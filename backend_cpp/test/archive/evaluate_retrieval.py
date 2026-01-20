import requests
import argparse
import sys
import statistics
from termcolor import colored

# Default configuration (can be overridden via ENV vars in real production)
DEFAULT_API_URL = "http://127.0.0.1:5002"

# Validation Set: Define what file "owns" what concept.
# In a real pipeline, this might load from a 'ground_truth.json' file.
TEST_CASES = [
    # (Concept/Question, Partial Filename Match)
    ("Where is the main entry point?", "main"),
    ("Where is the retrieval logic?", "retrieval_engine"),
    ("Where are the API routes defined?", "main.cpp"),
    ("Where is the embedding service?", "embedding_service"),
    ("Where is the vector store implementation?", "faiss_vector_store")
]

def evaluate(project_id, api_url):
    print(colored(f"🚀 STARTING AUDIT FOR PROJECT: {project_id}", "cyan", attrs=['bold']))
    print(f"📡 API Endpoint: {api_url}")

    scores = []

    for query, expected_keyword in TEST_CASES:
        print(f"\n🔎 Testing: '{query}'")
        
        try:
            payload = {
                "project_id": project_id,
                "prompt": query
            }
            
            # Hit the candidates endpoint
            res = requests.post(f"{api_url}/retrieve-context-candidates", json=payload)
            
            if res.status_code == 500:
                print(colored("   ❌ Server Error (500). Is the Project ID correct?", "red"))
                return False
            
            data = res.json()
            if "error" in data:
                print(colored(f"   ❌ API Error: {data['error']}", "red"))
                return False

            candidates = data.get('candidates', [])
            
            # Calculate Reciprocal Rank
            rank = 0
            found = False
            for i, cand in enumerate(candidates):
                # Case-insensitive path check
                if expected_keyword.lower() in cand['file_path'].lower():
                    rank = i + 1
                    found = True
                    break
            
            if found:
                reciprocal_rank = 1.0 / rank
                scores.append(reciprocal_rank)
                color = "green" if rank == 1 else "yellow"
                print(colored(f"   ✅ Found '{expected_keyword}' at Rank {rank}", color))
            else:
                scores.append(0.0)
                print(colored(f"   🔴 Failed to find '{expected_keyword}' in top {len(candidates)}", "red"))

        except Exception as e:
            print(colored(f"   ❌ Connection Failed: {e}", "red"))
            sys.exit(1)

    if not scores:
        print("No tests run.")
        return

    mrr = statistics.mean(scores)
    print(colored("\n" + "="*40, "white"))
    print(colored(f"🏆 FINAL MRR SCORE: {mrr:.3f}", "cyan", attrs=['bold']))
    print(colored("="*40, "white"))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Code Assistance Retrieval Auditor")
    parser.add_argument("project_id", type=str, help="The exact folder name inside 'data/' directory")
    parser.add_argument("--url", type=str, default=DEFAULT_API_URL, help="Backend API URL")
    
    args = parser.parse_args()
    
    evaluate(args.project_id, args.url)