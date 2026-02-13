import ast
import hashlib
import numpy as np
import google.generativeai as genai
import time
import re
import json
from browser_bridge import browser_bridge 

# Configure your embedding model
embedding_model = "models/text-embedding-001"

# --- CODE GRAPH ---
CODE_GRAPH_COLLECTION = "code_graph_nodes"
CODE_PROJECTS_COLLECTION = "code_projects" 

class CodeNode:
    def __init__(self, name, code, docstring, file_path, type="function"):
        self.id = f"{file_path}::{name}"
        self.name = name
        self.code = code
        self.docstring = docstring or ""
        self.file_path = file_path
        self.type = type
        self.dependencies = set()
        self.vector = []
        self.weights = {
            "structural": 0.5,
            "complexity": 0.5,
            "type_bias": 1.0
        }
        self.ai_summary = ""      # critic
        self.ai_dependencies = [] 
        self.ai_quality_score = 0.0 

    def to_dict(self):
        return {
            "id": self.id,
            "name": self.name,
            "file_path": self.file_path,
            "type": self.type,
            "content": self.code,
            "docstring": self.docstring,
            "dependencies": list(self.dependencies),
            "vector": self.vector,
            "weights": self.weights,
            "ai_summary": self.ai_summary,
            "ai_dependencies": self.ai_dependencies,
            "ai_quality_score": self.ai_quality_score
        }

def extract_functions_and_classes(file_content, file_path):
    nodes = []
    
    def calc_complexity(code_text):
        return min(len(code_text) / 300.0, 1.0) # More sensitive complexity

    def calc_type_bias(path):
        if path.endswith(('.py', '.js', '.ts', '.java', '.cpp', '.c', '.h')): return 1.0
        return 0.5

    def get_deps(node_scope):
        """Extracts anything that looks like a function call or class usage."""
        deps = set()
        try:
            for child in ast.walk(node_scope):
                # 1. Calls: obj.method() or func()
                if isinstance(child, ast.Call):
                    if isinstance(child.func, ast.Name):
                        deps.add(child.func.id)
                    elif isinstance(child.func, ast.Attribute):
                        deps.add(child.func.attr)
                # 2. Attribute access: self.variable
                elif isinstance(child, ast.Attribute):
                    deps.add(child.attr)
                # 3. Base classes
                elif isinstance(child, ast.ClassDef):
                    for base in child.bases:
                        if isinstance(base, ast.Name):
                            deps.add(base.id)
        except: pass
        return deps

    if file_path.endswith('.py'):
        try:
            tree = ast.parse(file_content)
            lines = file_content.splitlines()

            # --- 1. Handle Top-Level Functions ---
            for node in tree.body:
                if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                    start, end = node.lineno - 1, node.end_lineno
                    code_body = "\n".join(lines[start:end])
                    
                    graph_node = CodeNode(
                        name=node.name, # Top level: just name
                        code=code_body,
                        docstring=ast.get_docstring(node),
                        file_path=file_path,
                        type="function"
                    )
                    graph_node.dependencies = get_deps(node)
                    graph_node.weights['complexity'] = calc_complexity(code_body)
                    graph_node.weights['type_bias'] = calc_type_bias(file_path)
                    nodes.append(graph_node)

            # --- 2. Handle Classes and Methods (Qualified Naming) ---
            for node in tree.body:
                if isinstance(node, ast.ClassDef):
                    class_name = node.name
                    
                    # Add the Class itself
                    start, end = node.lineno - 1, node.end_lineno
                    # Don't capture whole class body if it's huge, just signature + docstring
                    # But for context, we might want the whole thing. Let's limit to 50 lines for the class node wrapper.
                    code_preview = "\n".join(lines[start:min(end, start+50)]) 
                    
                    class_node = CodeNode(
                        name=class_name,
                        code=code_preview,
                        docstring=ast.get_docstring(node),
                        file_path=file_path,
                        type="class"
                    )
                    class_node.dependencies = get_deps(node)
                    nodes.append(class_node)

                    # Add Methods
                    for item in node.body:
                        if isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef)):
                            method_name = f"{class_name}.{item.name}" # QUALIFIED NAME
                            m_start, m_end = item.lineno - 1, item.end_lineno
                            m_code = "\n".join(lines[m_start:m_end])
                            
                            method_node = CodeNode(
                                name=method_name,
                                code=m_code,
                                docstring=ast.get_docstring(item),
                                file_path=file_path,
                                type="method"
                            )
                            # Methods depend on other things in the file + class
                            method_node.dependencies = get_deps(item)
                            # Add implicit dependency on parent class
                            method_node.dependencies.add(class_name)
                            
                            method_node.weights['complexity'] = calc_complexity(m_code)
                            method_node.weights['type_bias'] = calc_type_bias(file_path)
                            nodes.append(method_node)

            # Fallback: If file is empty of functions/classes but has code
            if not nodes and len(file_content.strip()) > 0:
                 raise SyntaxError("No structures found, treat as file")

            return nodes

        except Exception:
            pass # Fallback to regex/file parsing

    # Non-Python or Fallback
    file_name = file_path.split('/')[-1]
    # Regex to find words that look like function calls
    defs = re.findall(r'(function|class|def)\s+(\w+)', file_content)
    
    if defs:
        for type_, name in defs:
            node = CodeNode(
                name=f"{file_name}::{name}",
                code=f"// Definition in {file_name}\n{type_} {name} ...",
                docstring="Extracted via Regex",
                file_path=file_path,
                type=type_
            )
            node.weights['complexity'] = 0.5
            node.weights['type_bias'] = calc_type_bias(file_path)
            nodes.append(node)
            
    # Always add the whole file as a node too for fallback
    file_node = CodeNode(
        name=file_name,
        code=file_content,
        docstring=f"Full File: {file_name}",
        file_path=file_path,
        type="file"
    )
    # Regex for calls
    file_node.dependencies = set(re.findall(r'(\w+)\(', file_content))
    file_node.weights['complexity'] = min(len(file_content)/1000, 1.0)
    file_node.weights['type_bias'] = calc_type_bias(file_path)
    
    nodes.append(file_node)
    return nodes

def generate_embeddings(nodes):
    if not nodes: return 
    
    # UPDATED: We now include the AI Summary in the text to embed
    texts = []
    for n in nodes:
        # If critic ran, n.docstring now contains the AI summary.
        # If not, it's just the normal code.
        content = f"Type: {n.type}. Name: {n.name}. \nSummary: {n.ai_summary}. \nCode:\n{n.code[:800]}"
        texts.append(content)

    try:
        # ... existing embedding logic ...
        results = genai.embed_content(model="models/text-embedding-004", content=texts, task_type="retrieval_document")
        for i, node in enumerate(nodes):
            node.vector = results['embedding'][i]
    except Exception as e:
        print(f"⚠️ Embedding failed: {e}")

def calculate_static_weights(all_nodes):
    # Boost popular nodes
    incoming_calls = {}
    for node in all_nodes:
        for dep in node.dependencies:
            incoming_calls[dep] = incoming_calls.get(dep, 0) + 1
            # Also count simple splits (e.g., if dep is "User.save", count "save")
            if '.' in dep:
                simple = dep.split('.')[-1]
                incoming_calls[simple] = incoming_calls.get(simple, 0) + 1
    
    max_calls = max(incoming_calls.values()) if incoming_calls else 1

    for node in all_nodes:
        # Match against full name OR simple name
        calls = incoming_calls.get(node.name, 0)
        if '.' in node.name:
            calls += incoming_calls.get(node.name.split('.')[-1], 0)
            
        node.weights['structural'] = 0.3 + (0.7 * (calls / max_calls))

def gaussian_retrieval(project_id, user_query, db_instance):
    """
    Implements the Normal Distribution / Mindmap traversal logic.
    """
    print(f"  🧠 Starting Gaussian Retrieval for: {user_query}")
    
    # 1. Vectorize User Query
    query_embedding = genai.embed_content(
        model=embedding_model,
        content=user_query,
        task_type="retrieval_query"
    )['embedding']
    
    # 2. Fetch All Nodes (In a real prod app, use a Vector DB. For now, fetch all and calc cosine locally)
    # This is fast enough for < 10,000 nodes.
    graph_ref = db_instance.collection(CODE_PROJECTS_COLLECTION).document(project_id).collection("code_graph_nodes")
    docs = graph_ref.stream()
    
    nodes = []
    for d in docs:
        nodes.append(d.to_dict())
        
    if not nodes:
        return "No code graph found. Please sync your project."

    # 3. Find the "Center" (The Focal Point)
    # Calculate Cosine Similarity
    query_vec = np.array(query_embedding)
    scored_nodes = []

    # --- TUNING KNOBS (Adjust these to change AI behavior) ---
    W_SEMANTIC = 0.65    # How much the vector match matters (Most important)
    W_STRUCTURAL = 0.15  # How much popularity matters
    W_COMPLEXITY = 0.10  # Prefer substantial code over tiny wrappers
    W_TYPE = 0.10        # Prefer .py/.dart over .json
    
    # Map for quick lookup by name
    node_map = {n['name']: n for n in nodes} 
    
    for node in nodes:
        if not node.get('vector'): continue
        
        # 1. Semantic Score (Vector Cosine Similarity)
        vec = np.array(node['vector'])
        semantic_score = np.dot(query_vec, vec) / (np.linalg.norm(query_vec) * np.linalg.norm(vec))
        
        # 2. Retrieve other dimensions (Default to 0.5 if missing)
        weights = node.get('weights', {'structural': 0.5, 'complexity': 0.5, 'type_bias': 0.5})
        
        # 3. Calculate Composite Score
        final_score = (
            (semantic_score * W_SEMANTIC) +
            (weights['structural'] * W_STRUCTURAL) +
            (weights['complexity'] * W_COMPLEXITY) +
            (weights['type_bias'] * W_TYPE)
        )
        
        scored_nodes.append((final_score, node))

    scored_nodes.sort(key=lambda x: x[0], reverse=True)

    if not scored_nodes:
        return "No relevant code found."

    center_node = scored_nodes[0][1] # Best match
    best_score = scored_nodes[0][0]

    print(f"  🎯 Focal Point: {center_node['name']} (Composite Score: {best_score:.3f})")

    # 4. Build the Context (The Gaussian Curve)
    context_parts = []
    included_ids = set()
    
    # --- ZONE 0: THE CENTER (Full Code) ---
    context_parts.append(f"# --- FOCAL POINT: {center_node['file_path']} ---\n{center_node['content']}\n")
    included_ids.add(center_node['id'])
    
    # --- ZONE 1: IMMEDIATE DEPENDENCIES (1 Sigma) ---
    # We look at what the center calls, AND what calls the center
    dependencies = center_node.get('dependencies', [])
    
    for dep_name in dependencies:
        neighbor = node_map.get(dep_name)
        if neighbor and neighbor['id'] not in included_ids:
            # WEIGHT CHECK: If it's a heavy weight (crucial util), show full code.
            # Otherwise, show signature/docstring.
            if neighbor['static_weight'] > 0.7:
                context_parts.append(f"# --- CRITICAL DEPENDENCY: {neighbor['file_path']} ---\n{neighbor['content']}\n")
            else:
                # Save tokens: Extract just the def line + docstring
                sig = neighbor['content'].split(':')[0] + ":"
                doc = neighbor['docstring']
                context_parts.append(f"# --- DEPENDENCY: {neighbor['file_path']} ---\n{sig}\n    \"\"\"{doc}\"\"\"\n    # ... (Code hidden) ...\n")
            
            included_ids.add(neighbor['id'])

    # --- ZONE 2: HIGH WEIGHT GLOBALS (The Background) ---
    # Include other high-weight nodes even if not directly connected (Global utilities)
    for node in nodes:
        if node['id'] not in included_ids and node['static_weight'] > 0.85:
             sig = node['content'].split(':')[0] + ":"
             context_parts.append(f"# --- GLOBAL CONTEXT: {node['name']} ---\n{sig}\n    # ... (Available Utility) ...\n")
             included_ids.add(node['id'])

    return "\n".join(context_parts)

def enrich_nodes_with_critic(nodes, *, model_instance, max_nodes_to_process=50):
    """
    Uses Browser Bridge (Google AI Studio) to analyze the purpose and quality of nodes.
    Removes the dependency on 'model_instance'.
    """
    print(f"  🕵️‍♂️ AI Critic starting analysis on {min(len(nodes), max_nodes_to_process)} nodes...")
    
    # Ensure bridge is running
    browser_bridge.start()

    count = 0
    for node in nodes:
        if count >= max_nodes_to_process: break
        if len(node.code) < 50: continue 
        
        try:
            prompt = f"""
            You are a Senior Code Reviewer. Analyze this Python code structure.
            
            NAME: {node.name}
            TYPE: {node.type}
            CODE:
            ```python
            {node.code[:2000]} 
            ```
            
            Task:
            1. Write a 1-sentence summary of the PURPOSE (what problem it solves).
            2. Identify implicit logical DEPENDENCIES (concepts, external libs, or specific classes it likely uses).
            3. Rate code QUALITY (0.0 to 1.0) based on clarity and naming.

            Return ONLY valid JSON:
            {{ "summary": "...", "dependencies": ["dep1", "dep2"], "quality": 0.9 }}
            """
            
            # --- USE BRIDGE ---
            response_text = browser_bridge.send_prompt(prompt)
            
            # Clean markdown json blocks if present
            raw_text = response_text.strip()
            if raw_text.startswith("```"):
                match = re.search(r"```[a-zA-Z]*\n?(.*?)```", raw_text, re.DOTALL)
                if match:
                    raw_text = match.group(1).strip()
                else:
                    raw_text = re.sub(r"^```[a-zA-Z]*\n?|```$", "", raw_text).strip()

            try:
                data = json.loads(raw_text)
            except json.JSONDecodeError as e:
                print(f"警告: Critic JSON 解析失败: {e}")
                data = {"summary": f"Parse error from bridge", "dependencies": [], "quality": 0.5}
                        
            # Update Node
            node.ai_summary = data.get("summary", "")
            node.ai_dependencies = data.get("dependencies", [])
            node.ai_quality_score = float(data.get("quality", 0.5))
            
            # Update weights
            node.weights['quality'] = node.ai_quality_score
            
            # IMPORTANT: Enhance the docstring so Vector Search finds this summary!
            node.docstring = f"[AI SUMMARY]: {node.ai_summary}\n\n{node.docstring}"
            
            print(f"    ✅ Critic analyzed: {node.name} (Score: {node.ai_quality_score})")
            count += 1
            
        except Exception as e:
            print(f"    ⚠️ Critic failed for {node.name}: {e}")