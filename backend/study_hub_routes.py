# backend/study_hub_routes.py
import re
import markdown
import html2text
from flask import Blueprint, request, jsonify
from firebase_admin import firestore
from utils import L1_CACHE
import random
import time 
from pathlib import Path 
import google.api_core.exceptions
from browser_bridge import browser_bridge
from utils import extract_text, delete_collection, split_chunks, convert_pptx_to_pdf_windows
import redis
from google.genai import types 
import os
import tempfile

# --- FROM CONFIG ---
from config import (
    STUDY_PROJECTS_COLLECTION,
    CODE_PROJECTS_COLLECTION,
    CODE_FILES_SUBCOLLECTION,
    VECTOR_STORE_ROOT,
    NULL_CACHE_VALUE
)

from code_graph_engine import (
    FaissVectorStore, 
    CrossEncoderReranker,
    hybrid_retrieval_pipeline,
    build_hierarchical_context
)

from services import ai_client, note_generation_model, db, chat_model, cache_manager

cross_encoder = CrossEncoderReranker()
study_hub_bp = Blueprint('study_hub_bp', __name__)

# --- HELPER FUNCTIONS for Study Hub ---
def get_original_text(project_id, source_id):
    """Fetches and reassembles the original, unprocessed text for a specific source."""
    print(f"  📚 Retrieving original text for source '{source_id}' in project '{project_id}'...")
    
    try:
        source_ref = db.collection(STUDY_PROJECTS_COLLECTION).document(project_id).collection('sources').document(source_id)
        chunks_query = source_ref.collection('chunks').order_by('order').stream()
        
        all_chunks = []
        page_count = 0
        for chunk_page in chunks_query:
            page_count += 1
            page_data = chunk_page.to_dict()
            chunks_in_page = page_data.get('chunks', [])
            all_chunks.extend(chunks_in_page)
            print(f"    - Fetched page {page_count}, found {len(chunks_in_page)} chunks.")
        
        if page_count == 0:
            print("    - ‼️  Query returned 0 documents from the 'chunks' subcollection.")

        full_text = "\n".join(all_chunks)
        print(f"  ✅ Assembled {len(full_text)} characters of original text from {len(all_chunks)} total chunks.")
        return full_text
    except Exception as e:
        print(f"  ❌ An error occurred while fetching original text: {e}")
        return "" # Return empty on failure
"""
    🛑 ABSOLUTE OUTPUT RULE (MUST FOLLOW):

    - Your ENTIRE response MUST be inside ONE Markdown code block.
    - NOTHING is allowed outside the code block.
    - No explanations, no comments, no greetings outside the code block.
    - If any text appears outside the code block, the answer is WRONG.
    - DO NOT use triple backticks (```) anywhere inside the note.
    - If the original note contains code:
        - Rewrite the code as plain text
        - Indent it with spaces OR
        - Put it under a bullet point
        - Label it clearly as **Code Example (Plain Text)**
        """
def generate_note(text):
    """Generates a simplified study note using the Browser Bridge (Direct)."""
    print("  🤖 Generating AI study note via Browser Bridge...")
    prompt = f"""
    You are given a raw study note.

    Your task is to rewrite it into a simpler and clearer version for students, WITHOUT removing, skipping, summarizing away, or merging any information from the original note.

    ⚠️ ABSOLUTE RULE (Very Important):

    - Every sentence, explanation, example, and detail in the raw note MUST appear in the output.

    - You may reword sentences using simpler words, but you are NOT allowed to delete or shorten ideas.

    - If something exists in the raw note, it must also exist in the final note.

    📝 CRITICAL OUTPUT RULE (The "Wrapper"):
    1. Headings:
        - Use # for main titles
        - Use ## for sections
    
    2. Bold Keywords:
        - You MUST bold (**text**) all key terms, definitions, and important concepts

    3. Dividers:
        - Insert a horizontal rule (---) between every major section

    4. Lists:
        - Use bullet points (*)

    ✏️ Content Rules:

    1. Do NOT skip any point or explanation from the original note.

    2. Shorten the sentence, and change unfamiliar word to more common words, but do not skip any points or explanations.

    3. The output language must be the same as the source note (do not translate the whole note).

    4. For any unfamiliar word, add a Chinese explanation (中文解释) right after it in brackets.

    5. Add some emojis to make the note friendly (do not overuse).

    6. After each section, add a Mnemonic Tip to help students remember.

    🎯 Goal:

    Make the note shorter, clearer, and student-friendly, while keeping 100% of the original information.

    If yes, generate the simplified note for the text below:

    {text} 
    """
    try:
        # --- DIRECT BROWSER BRIDGE USAGE ---
        # Ensure bridge thread is running
        browser_bridge.start()
        
        response_text = browser_bridge.send_prompt(prompt, use_clipboard=True)
        print("  ✅ Browser Bridge response received.")
        clean_text = response_text.strip()
        
        # Remove Markdown fences if present
        if clean_text.startswith("```markdown"):
            clean_text = clean_text.replace("```markdown", "", 1)
        elif clean_text.startswith("```"):
            clean_text = clean_text.replace("```", "", 1)
        if clean_text.endswith("```"):
            clean_text = clean_text[:-3]

        # Fix HTML entities
        clean_text = re.sub(r'(#include\s*)<([^>]+)>', r'\1&lt;\2&gt;', clean_text)
        
        return markdown.markdown(clean_text, extensions=['tables'])
    except Exception as e:
        print(f"  ❌ Browser Bridge Note Generation Failed: {e}")
        raise

def get_simplified_note_context(project_id, source_id=None):
    """Fetches and combines all simplified note pages into clean text for the chatbot."""
    print(f"  📚 Retrieving simplified note context for project {project_id}...")
    full_html_content = ""
    sources_to_query = []

    if source_id:
        source_ref = db.collection(STUDY_PROJECTS_COLLECTION).document(project_id).collection('sources').document(source_id)
        sources_to_query.append(source_ref)
    else:
        sources_stream = db.collection(STUDY_PROJECTS_COLLECTION).document(project_id).collection('sources').stream()
        sources_to_query = [source.reference for source in sources_stream]

    for source_ref in sources_to_query:
        pages_query = source_ref.collection('note_pages').order_by('order').stream()
        for page in pages_query:
            full_html_content += page.to_dict().get('html', '')

    if not full_html_content:
        print("  ⚠️ No note content found.")
        return ""

    # Convert HTML back to clean text for the AI model
    h = html2text.HTML2Text()
    h.ignore_links = True
    h.ignore_images = True
    clean_text = h.handle(full_html_content)
    
    print(f"  ✅ Assembled {len(clean_text)} characters of simplified note text.")
    return clean_text

# --- ROUTES ---

# --- STUDY HUB PROJECT ROUTES ---
@study_hub_bp.route('/get-projects', methods=['GET'])
def get_projects():
    docs = db.collection(STUDY_PROJECTS_COLLECTION).order_by('timestamp', direction=firestore.Query.DESCENDING).stream()
    projects = [{"id": d.id, "name": d.to_dict().get('name')} for d in docs]
    return jsonify(projects)

@study_hub_bp.route('/create-project', methods=['POST'])
def create_project():
    name = request.json.get('name')
    
    ref = db.collection(STUDY_PROJECTS_COLLECTION).document()
    ref.set({
        'name': name,
        'timestamp': firestore.SERVER_TIMESTAMP,
    })
    return jsonify({"id": ref.id})

@study_hub_bp.route('/rename-project/<project_id>', methods=['PUT'])
def rename_project(project_id):
    new_name = request.json.get('new_name')
    if not new_name:
        return jsonify({"success": False, "error": "New name not provided"}), 400
    try:
        project_ref = db.collection(STUDY_PROJECTS_COLLECTION).document(project_id)
        project_ref.update({'name': new_name})
        return jsonify({"success": True, "message": f"Project renamed to {new_name}."}), 200
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500

@study_hub_bp.route('/delete-project/<project_id>', methods=['DELETE'])
def delete_project(project_id):
    print(f"\n🗑️  DELETE REQUEST for project: {project_id}")
    try:
        project_ref = db.collection(STUDY_PROJECTS_COLLECTION).document(project_id)
        for collection_ref in project_ref.collections():
            delete_collection(collection_ref, batch_size=50)
        project_ref.delete()
        print(f"✅ Successfully deleted project: {project_id}")
        return jsonify({"success": True}), 200
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500
        
# --- NEW: CODE ASSISTANT PROJECT ROUTES ---
@study_hub_bp.route('/get-code-projects', methods=['GET'])
def get_code_projects():
    docs = db.collection(CODE_PROJECTS_COLLECTION).order_by('timestamp', direction=firestore.Query.DESCENDING).stream()
    projects = [{"id": d.id, "name": d.to_dict().get('name')} for d in docs]
    return jsonify(projects)

@study_hub_bp.route('/create-code-project', methods=['POST'])
def create_code_project():
    name = request.json.get('name')
    ref = db.collection(CODE_PROJECTS_COLLECTION).document()
    ref.set({'name': name, 'timestamp': firestore.SERVER_TIMESTAMP})
    return jsonify({"id": ref.id})

@study_hub_bp.route('/get-sources/<project_id>', methods=['GET'])
def get_sources(project_id):
    docs = db.collection(STUDY_PROJECTS_COLLECTION).document(project_id).collection('sources').stream()
    sources = [{"id": d.id, "filename": d.to_dict().get('filename')} for d in docs]
    return jsonify(sources)

@study_hub_bp.route('/upload-source/<project_id>', methods=['POST'])
def upload_source(project_id):
    print(f"\n📁 UPLOAD REQUEST for project: {project_id}")
    
    if 'pdfs' not in request.files and not request.files:
         return jsonify({"error": "No files provided", "success": False}), 400
    
    files = request.files.getlist('pdfs')
    if not files and request.files:
        first_key = list(request.files.keys())[0]
        files = request.files.getlist(first_key)

    if not files or files[0].filename == '':
        return jsonify({"error": "No files selected", "success": False}), 400
    
    processed, errors = [], []
    
    # Ensure bridge is ready
    browser_bridge.start()

    for file in files:
        filename = file.filename
        ext = "." + filename.split('.')[-1].lower()
        safe_id = re.sub(r'[.#$/[\]]', '_', filename)
        print(f"\n🔄 Processing '{filename}' via AI Studio Extraction...")

        temp_path = None
        pdf_path = None

        try:
            # 1. Save uploaded file to temp
            with tempfile.NamedTemporaryFile(delete=False, suffix=ext) as tmp:
                file.stream.seek(0)
                file.save(tmp.name)
                temp_path = tmp.name

            target_upload_path = temp_path

            # 2. Handle PPTX -> PDF Conversion
            if ext == '.pptx':
                print("    👉 Detected PPTX. Converting to PDF for AI Studio...")
                pdf_path = temp_path.replace(".pptx", ".pdf")
                success = convert_pptx_to_pdf_windows(temp_path, pdf_path)
                if success and os.path.exists(pdf_path):
                    target_upload_path = pdf_path
                    print("    ✅ Conversion successful.")
                else:
                    raise Exception("PPTX to PDF conversion failed.")

            # 3. Call Browser Bridge to Extract Text
            print(f"    🤖 Sending {os.path.basename(target_upload_path)} to AI Studio for extraction...")
            extracted_text = browser_bridge.extract_text_from_file(target_upload_path)

            if not extracted_text or "Error:" in extracted_text[:20]:
                raise Exception(f"AI Extraction Failed: {extracted_text}")

            print(f"    ✅ Text Extracted: {len(extracted_text)} characters.")

            # 4. Save Metadata to Firestore
            source_ref = db.collection(STUDY_PROJECTS_COLLECTION).document(project_id).collection('sources').document(safe_id)
            source_ref.set({
                'filename': filename, 
                'timestamp': firestore.SERVER_TIMESTAMP, 
                'character_count': len(extracted_text)
            })
            
            # 5. Save Original Text Chunks
            text_chunks = split_chunks(extracted_text)
            for i in range(0, len(text_chunks), 100):
                batch = text_chunks[i:i+100]
                page_num = i // 100
                source_ref.collection('chunks').document(f'page_{page_num}').set({
                    'chunks': batch, 
                    'order': page_num
                })
            
            # 6. Generate Note (Using the extracted text)
            try:
                note_html = generate_note(extracted_text)
                
                chunk_size = 900000 
                for i in range(0, len(note_html), chunk_size):
                    chunk = note_html[i:i+chunk_size]
                    page_num = i // chunk_size
                    source_ref.collection('note_pages').document(f'page_{page_num}').set({
                        'html': chunk, 
                        'order': page_num
                    })
                print(f"    ✅ Generated and saved study note")
                
            except Exception as note_error:
                print(f"    ⚠️ Note generation failed: {note_error}")
                source_ref.collection('note_pages').document('page_0').set({
                    'html': f'<p>Note generation failed: {note_error}</p>',
                    'order': 0
                })
            
            processed.append({"filename": filename, "id": safe_id})
            
        except Exception as e:
            import traceback
            print(f"❌ CRITICAL ERROR processing '{filename}': {e}")
            traceback.print_exc()
            errors.append({"filename": filename, "error": str(e)})
        
        finally:
            # Cleanup Temps
            try:
                if temp_path and os.path.exists(temp_path):
                    os.remove(temp_path)
                if pdf_path and os.path.exists(pdf_path):
                    os.remove(pdf_path)
            except: pass

    return jsonify({"success": len(processed) > 0, "processed": processed, "errors": errors})

@study_hub_bp.route('/delete-source/<project_id>/<path:source_id>', methods=['DELETE'])
def delete_source(project_id, source_id):
    print(f"\n🗑️  DELETE REQUEST for source: {source_id}")
    try:
        source_ref = db.collection(STUDY_PROJECTS_COLLECTION).document(project_id).collection('sources').document(source_id)
        for collection_ref in source_ref.collections():
            delete_collection(collection_ref, batch_size=50)
        source_ref.delete()

        # Invalidate Cache using unified CacheManager
        if cache_manager:
            note_key = f"note:{project_id}:{source_id}"
            cache_manager.delete(note_key)
            print(f"✅ Invalidated cache for deleted source: {note_key}")

        print(f"✅ Successfully deleted source document: {source_id}")
        return jsonify({"success": True, "message": f"Source {source_id} deleted."}), 200

    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500

@study_hub_bp.route('/get-note/<project_id>/<path:source_id>')
def get_note(project_id, source_id):
    # 1. Define the cache key
    cache_key = f"note:{project_id}:{source_id}"

    # 2. Define the factory function (what to do if cache misses)
    def fetch_note_from_db():
        print(f"  🔍 Fetching note from Firestore for {cache_key}...")
        pages_query = db.collection(STUDY_PROJECTS_COLLECTION)\
            .document(project_id).collection('sources').document(source_id)\
            .collection('note_pages').order_by('order').stream()
        
        html = "".join(p.to_dict().get('html', '') for p in pages_query)
        
        # Return empty string if nothing found
        return html if html else ""

    # 3. Use CacheManager
    if cache_manager:
        try:
            note_html = cache_manager.get_or_set(
                key=cache_key,
                factory=fetch_note_from_db,
                ttl_l1=300,   # 5 mins in memory
                ttl_l2=3600   # 1 hour in Redis
            )
            return jsonify({"note_html": note_html})
        except Exception as e:
            print(f"Cache Error: {e}")
            # Fallback if cache fails
            return jsonify({"note_html": fetch_note_from_db()})
    else:
        # Fallback if no cache manager
        return jsonify({"note_html": fetch_note_from_db()})

@study_hub_bp.route('/update-note/<project_id>/<path:source_id>', methods=['POST'])
def update_note(project_id, source_id):
    new_html = request.json.get('html_content')
    if new_html is None:
        return jsonify({"error": "Missing 'html_content'"}), 400
    try:
        source_ref = db.collection(STUDY_PROJECTS_COLLECTION).document(project_id).collection('sources').document(source_id)
        note_pages_ref = source_ref.collection('note_pages')
        
        for doc in note_pages_ref.stream():
            doc.reference.delete()
        
        chunk_size = 900000
        note_pages_saved = 0
        for i in range(0, len(new_html), chunk_size):
            chunk = new_html[i:i+chunk_size]
            page_num = i // chunk_size
            
            note_pages_ref.document(f'page_{page_num}').set({
                'html': chunk,
                'order': page_num
            })
            note_pages_saved += 1
            print(f"  + Saved note page {page_num}")

        # Invalidate Cache
        if cache_manager:
            cache_key = f"note:{project_id}:{source_id}"
            cache_manager.delete(cache_key)
            print(f"✅ Invalidated cache for {cache_key}")
        
        print(f"✅ Note updated successfully. {note_pages_saved} pages saved.")
        return jsonify({"success": True, "message": "Note updated successfully"}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

# ... imports ...

@study_hub_bp.route('/ask-chatbot/<project_id>', methods=['POST'])
def ask_chatbot(project_id):
    data = request.json
    question = data.get('question')
    source_id = data.get('source_id') 
    history = data.get('history', []) 
    
    context = get_simplified_note_context(project_id, source_id)
    if not context:
        return jsonify({"answer": "I couldn't find any generated notes to read. Please upload a document first!"})

    # --- UPDATED SYSTEM PROMPT ---
    system_prompt = f"""You are an intelligent study assistant. Your goal is to help the user understand the provided study notes.

    ### 🧠 Guidelines for Answering:
    1.  **Source of Truth:** Base your answers on the provided notes.
    2.  **Allowed Actions:** You ARE allowed to **summarize**, **rephrase**, **simplify**, or **structure** the information.
    3.  **Handling Missing Info:** Only refuse to answer if the specific *topic* is completely absent from the notes.

    ### 📝 CRITICAL OUTPUT RULE (The "Wrapper"):
    To preserve formatting, you **MUST** wrap your ENTIRE response inside a Markdown code block.

    Here are the study notes you must use:
    ---
    {context}
    ---
    """

    try:
        browser_bridge.start()
        
        # CHANGE: If you want a TRULY new chat every time, 
        # do not include 'history' in the flat_prompt.
        flat_prompt = f"SYSTEM: {system_prompt}\n\n"
        
        # Comment this loop out if you don't want the AI to remember 
        # previous questions from the current session:
        # for turn in history:
        #     role = turn.get('role', 'user').upper()
        #     content = turn.get('content', '')
        #     flat_prompt += f"{role}: {content}\n"
            
        flat_prompt += f"USER: {question}\n"
        flat_prompt += "MODEL: "

        # Send to Browser (which will now refresh the page first)
        raw_answer = browser_bridge.send_prompt(flat_prompt)
        print("  ✅ Browser Bridge response received.")
        
        # --- CLEANING LOGIC ---
        # We strip the wrapper we asked for, leaving the raw Markdown behind.
        clean_answer = raw_answer.strip()
        clean_answer = clean_answer.replace("code Markdown download", "")
        clean_answer = clean_answer.replace("code\nMarkdown\ndownload", "")
        clean_answer = re.sub(r'^```[a-zA-Z]*\s*', '', clean_answer.strip())
        if clean_answer.endswith("```"):
            clean_answer = clean_answer[:-3].strip()
        
        # Remove the opening ```markdown or ```
        if "```markdown" in clean_answer:
            clean_answer = clean_answer.replace("```markdown", "", 1)
        elif clean_answer.startswith("```"):
            clean_answer = clean_answer.replace("```", "", 1)
            
        # Remove the closing ```
        if clean_answer.endswith("```"):
            clean_answer = clean_answer.substring(0, len(clean_answer) - 3) if hasattr(clean_answer, 'substring') else clean_answer[:-3]

        return jsonify({"answer": clean_answer.strip()})

    except Exception as e:
        print(f"  ❌ Error during chatbot generation: {e}")
        return jsonify({"answer": f"Sorry, an error occurred: {e}"})

@study_hub_bp.route('/generate-topic-note/<project_id>', methods=['POST'])
def topic_note(project_id):
    topic = request.json.get('topic')
    print(f"  🔍 Extracting topic '{topic}' from simplified notes...")

    simplified_notes_context = get_simplified_note_context(project_id)
    if not simplified_notes_context:
        return jsonify({"note_html": "<p>Could not find any notes to search through.</p>"})

    prompt = f"""
    You are an information retrieval assistant. Your task is to act like a "smart search".
    Given a TOPIC and existing STUDY NOTES, find and extract all sections, headings, paragraphs, and bullet points from the STUDY NOTES that are relevant to the TOPIC.

    RULES:
    1.  EXTRACT ONLY: Do NOT write new sentences or summaries. Your output must be a direct copy of relevant parts from the notes.
    2.  PRESERVE FORMATTING: Keep the original markdown formatting (headings, bold text, etc.).
    3.  NO COMMENTARY: Do not add text like "Here are the relevant sections...". Start immediately with the first extracted piece of content. If nothing is found, return only: "I could not find any information about that topic in the notes."

    ---
    TOPIC: {topic}
    ---
    EXISTING STUDY NOTES:
    {simplified_notes_context}
    ---
    """
    
    try:
        # --- DIRECT BROWSER BRIDGE USAGE ---
        browser_bridge.start()
            
        response_text = browser_bridge.send_prompt(prompt)
        html = markdown.markdown(response_text, extensions=['tables'])
        return jsonify({"note_html": html})
    except Exception as e:
        return jsonify({"note_html": f"<p>Error generating topic note: {e}</p>"})
    
@study_hub_bp.route('/regenerate-note/<project_id>/<path:source_id>', methods=['POST'])
def regenerate_note(project_id, source_id):
    print(f"\n🔄 REGENERATE NOTE request for source: {source_id} in project: {project_id}")
    try:
        original_text = get_original_text(project_id, source_id)
        if not original_text:
            return jsonify({"error": "Original source text not found or is empty. Please re-upload the document."}), 404

        browser_bridge.reset()
        new_note_html = generate_note(original_text)
        
        source_ref = db.collection(STUDY_PROJECTS_COLLECTION).document(project_id).collection('sources').document(source_id)
        note_pages_ref = source_ref.collection('note_pages')
        
        for doc in note_pages_ref.stream():
            doc.reference.delete()
            
        chunk_size = 900000 
        for i in range(0, len(new_note_html), chunk_size):
            page_content = new_note_html[i:i + chunk_size]
            page_num = i // chunk_size
            note_pages_ref.document(f'page_{page_num}').set({
                'html': page_content, 
                'order': page_num
            })
            print(f"  + Saved note page {page_num}")

        # Invalidate Cache
        if cache_manager:
            note_key = f"note:{project_id}:{source_id}"
            cache_manager.delete(note_key)
            print(f"  ✅ Invalidated cache for regenerated note: {note_key}")

        print(f"  ✅ SUCCESS: Note for '{source_id}' regenerated.")
        return jsonify({"success": True, "note_html": new_note_html})

    except Exception as e:
        import traceback
        print(f"❌ CRITICAL ERROR regenerating note '{source_id}': {e}")
        traceback.print_exc()
        return jsonify({"error": str(e)}), 500

@study_hub_bp.route('/generate-code-suggestion', methods=['POST'])
def generate_code_suggestion():
    data = request.json
    project_id = data.get('project_id')
    prompt_text = data.get('prompt')
    
    print("\n" + "="*80)
    print(f"🚀 Generating Code Suggestion via Browser Bridge")
    print(f"Project: {project_id}")
    print(f"Query: {prompt_text}")
    print("="*80)
    
    if not all([project_id, prompt_text]):
        return jsonify({"error": "Missing 'project_id' or 'prompt'"}), 400

    try:
        # --- 1. Load Vector Store ---
        store_path = Path("vector_stores") / project_id
        
        if not store_path.exists():
            return jsonify({
                "suggestion": "⚠️ This project hasn't been synced yet. Please run 'Check Synchronize' first."
            })
        
        print(f"  📂 Loading vector store from {store_path}...")
        vector_store = FaissVectorStore.load(store_path)
        
        # --- 2. Run Hybrid Retrieval (HyDE will now use Browser Bridge internally) ---
        context = hybrid_retrieval_pipeline(
            project_id=project_id,
            user_query=prompt_text,
            db_instance=db,
            vector_store=vector_store,
            cross_encoder=cross_encoder,
            use_hyde=True 
        )
        
        # --- 3. Generate Final Answer ---
        final_prompt = f"""
        ### ROLE
        You are a Senior Software Architect and Codebase Expert. You are assisting a developer by analyzing the provided code context to answer their questions accurately.

        ### CONTEXT (Retrieved Code)
        The following text contains the most relevant files and code snippets from the project. The format is: `# FILE: <path>` followed by the code.
        --------------------------------------------------
        {context}
        --------------------------------------------------

        ### USER QUESTION
        {prompt_text}

        ### INSTRUCTIONS
        1. **Source-Based Truth:** Answer ONLY based on the code provided in the CONTEXT. If the answer is not in the context, admit it. Do not make up functions or files that do not exist.
        2. **Citation:** When explaining logic, explicitly mention the filename (e.g., "In `backend/app.py`...") so the user knows where to look.
        3. **Actionable Output:**
        - If explaining concepts: Use clear, high-level summaries followed by technical details.
        - If fixing bugs: Explain the root cause, then provide the corrected code block.
        - If writing new code: Ensure it matches the style and patterns found in the CONTEXT.
        4. **Formatting:** Use Markdown. Use code blocks (```language) for code. Use bold text for variable names or file paths.

        ### ANSWER
        """

        print("  🤖 Sending final prompt to Browser Bridge...")
        
        # --- DIRECT BROWSER BRIDGE USAGE ---
        browser_bridge.start()
        suggestion_text = browser_bridge.send_prompt(final_prompt)

        print("="*80)
        print("✅ Response generated successfully")
        print("="*80 + "\n")
        
        return jsonify({"suggestion": suggestion_text})
    
    except FileNotFoundError as e:
        return jsonify({
            "suggestion": f"⚠️ Vector store not found: {e}. Please sync your project first."
        })
    except Exception as e:
        import traceback
        print(f"  ❌ CRITICAL ERROR: {e}")
        print(traceback.format_exc())
        return jsonify({"error": str(e)}), 500

@study_hub_bp.route('/retrieve-context-candidates', methods=['POST'])
def retrieve_candidates():
    data = request.json
    project_id = data.get('project_id')
    prompt_text = data.get('prompt')
    
    try:
        store_path = VECTOR_STORE_ROOT / project_id
        if not store_path.exists():
             return jsonify({"error": "Project index not found"}), 404

        vector_store = FaissVectorStore.load(store_path)
        
        # Get candidates with return_nodes_only=True
        # HyDE in pipeline will now use Bridge
        candidates = hybrid_retrieval_pipeline(
            project_id=project_id,
            user_query=prompt_text,
            db_instance=db,
            vector_store=vector_store,
            cross_encoder=cross_encoder,
            use_hyde=True,
            return_nodes_only=True 
        )
        
        return jsonify({"candidates": candidates})
    except Exception as e:
        print(f"Error retrieving candidates: {e}")
        return jsonify({"error": str(e)}), 500

@study_hub_bp.route('/generate-answer-from-context', methods=['POST'])
def generate_answer_from_context():
    data = request.json
    project_id = data.get('project_id')
    prompt_text = data.get('prompt')
    selected_ids = data.get('selected_ids', [])

    if not selected_ids:
        return jsonify({"suggestion": "❌ No context selected. Please select at least one file for me to analyze."})

    print(f"📥 Received {len(selected_ids)} IDs for generation: {selected_ids}")
    
    try:
        store_path = VECTOR_STORE_ROOT / project_id
        vector_store = FaissVectorStore.load(store_path)
        
        # Re-fetch full node content
        selected_nodes = []
        for uid in selected_ids:
            node_data = vector_store.get_node_by_name(uid)
            if node_data:
                selected_nodes.append({'node': node_data})
            else:
                print(f"⚠️ Node not found for ID: {uid}")
        
        # Build Context
        context = build_hierarchical_context(selected_nodes)
        
        final_prompt = f"""
        ### ROLE
        You are a Senior Software Architect.

        ### CONTEXT
        {context}

        ### QUESTION
        {prompt_text}

        ### INSTRUCTION
        Answer based on the code above.
        """
        
        # --- DIRECT BROWSER BRIDGE USAGE ---
        browser_bridge.start()
        
        answer = browser_bridge.send_prompt(final_prompt)
        return jsonify({"suggestion": answer})
        
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@study_hub_bp.route('/bridge/get-models', methods=['GET'])
def get_bridge_models():
    """Fetches available Gemini models AND the active one from AI Studio."""
    try:
        # We assume you added get_bridge_state() to browser_bridge.py 
        # as suggested in the previous step
        state = browser_bridge.get_bridge_state() 
        
        return jsonify({
            "models": state.get("models", []),
            "current_active": state.get("active") # This is what we need!
        })
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@study_hub_bp.route('/bridge/set-model', methods=['POST'])
def set_bridge_model():
    """Switches the active model in the Bridge."""
    model_name = request.json.get('model_name')
    if not model_name:
        return jsonify({"error": "No model name provided"}), 400
    
    try:
        success = browser_bridge.set_model(model_name)
        if success is True:
            return jsonify({"success": True, "message": f"Switched to {model_name}"})
        else:
            return jsonify({"success": False, "error": str(success)}), 500
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500

@study_hub_bp.route('/bridge/generate', methods=['POST'])
def bridge_generate():
    data = request.json
    prompt = data.get('prompt')
    
    if not prompt: return jsonify({"error": "No prompt provided"}), 400

    print(f"🌉 [Bridge] Offloading request from C++ (Length: {len(prompt)})")

    try:
        browser_bridge.start()
        
        # We don't force reset here anymore to keep it fast, 
        # the browser_bridge internal logic handles navigation if needed.
        
        response_text = browser_bridge.send_prompt(prompt)
        
        # 🚀 FIX: Detect Browser Errors
        if not response_text or response_text.startswith("Browser Error") or response_text.startswith("Error:"):
            print(f"❌ [Bridge] Failed: {response_text}")
            # Return 500 so C++ switches to API
            return jsonify({"success": False, "error": response_text}), 500

        print(f"   [Bridge] Success (Length: {len(response_text)})")
        
        return jsonify({
            "success": True,
            "text": response_text,
            "estimated_tokens": len(response_text) // 4
        })

    except Exception as e:
        print(f"❌ [Bridge] Exception: {e}")
        return jsonify({"success": False, "error": str(e)}), 500