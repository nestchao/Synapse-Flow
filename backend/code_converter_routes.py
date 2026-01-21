# backend/code_converter_routes.py
import json
import hashlib
from pathlib import Path
from flask import Blueprint, request, jsonify
from firebase_admin import firestore
from utils import get_file_hash, DOT_REPLACEMENT
from services import db

# --- BLUEPRINT SETUP ---
code_converter_bp = Blueprint('code_converter_bp', __name__)

# --- Collection names as constants ---
CODE_PROJECTS_COLLECTION = "code_projects"
CODE_FILES_SUBCOLLECTION = "synced_code_files"

@code_converter_bp.route('/code-converter/structure/<project_id>', methods=['GET'])
def get_dynamic_project_structure(project_id):
    """Builds file tree including special files (tree.txt and _full_context.txt)"""
    try:
        print(f"🌳 Building dynamic file tree for code_project: {project_id}")
        
        manifest_ref = db.collection(CODE_PROJECTS_COLLECTION).document(project_id).collection(CODE_FILES_SUBCOLLECTION).document('_manifest')
        manifest_doc = manifest_ref.get()

        tree = {}
        file_count = 0 

        if manifest_doc.exists:
            files_map = manifest_doc.to_dict().get('files', {})
            file_count = len(files_map)
            print(f"  - Found {file_count} entries in manifest. Building tree...")
            
            for path_str, data in files_map.items():
                doc_id = data.get('doc_id')
                if not doc_id:
                    continue
                
                parts = Path(path_str).parts
                d = tree
                for part in parts[:-1]:
                    d = d.setdefault(part, {})
                d[parts[-1]] = doc_id
        else:
            print("  - WARNING: Manifest document does not exist for this project yet.")

        converted_files_coll = db.collection(CODE_PROJECTS_COLLECTION).document(project_id).collection(CODE_FILES_SUBCOLLECTION)

        # Add special files
        print("  - Checking for special files...")
        if converted_files_coll.document('project_tree_txt').get().exists:
            tree['tree.txt'] = 'project_tree_txt'
            file_count += 1
            print("  - Found tree.txt")

        if converted_files_coll.document('project_full_context_txt').get().exists:
            tree['_full_context.txt'] = 'project_full_context_txt'
            file_count += 1
            print("  - Found _full_context.txt")
        
        print(f"  ✅ Built final tree with {file_count} total items.")
        return jsonify(tree)

    except Exception as e:
        import traceback
        error_details = traceback.format_exc()
        print(f"  ❌ CRITICAL ERROR building dynamic tree: {e}\n{error_details}")
        return jsonify({"error": str(e), "traceback": error_details}), 500


@code_converter_bp.route('/code-converter/file/<project_id>/<doc_id>', methods=['GET'])
def get_converted_file(project_id, doc_id):
    """
    Retrieves a file's content, automatically reassembling chunks if needed.
    Handles both regular files and chunked _full_context.txt
    """
    try:
        doc_ref = db.collection(CODE_PROJECTS_COLLECTION).document(project_id).collection(CODE_FILES_SUBCOLLECTION).document(doc_id)
        doc = doc_ref.get()
        
        if not doc.exists:
            return jsonify({"error": "File not found"}), 404
        
        data = doc.to_dict()
        
        # Check if this is a chunked document
        if data.get('is_chunked', False):
            print(f"  🔄 Retrieving chunked content for {doc_id}...")
            return jsonify(reassemble_chunked_content(doc_ref, data))
        
        # Regular document - return as-is
        return jsonify(data)
        
    except Exception as e:
        import traceback
        print(f"  ❌ Error retrieving file: {e}")
        print(traceback.format_exc())
        return jsonify({"error": str(e)}), 500


def reassemble_chunked_content(doc_ref, metadata):
    """
    Reassembles chunked content from subcollection.
    Returns a dict with the full content.
    """
    total_chunks = metadata.get('total_chunks', 0)
    print(f"    - Reassembling {total_chunks} chunks...")
    
    chunks_ref = doc_ref.collection('chunks').order_by('order').stream()
    
    chunks_data = []
    chunk_count = 0
    
    for chunk_doc in chunks_ref:
        chunk_data = chunk_doc.to_dict()
        chunks_data.append(chunk_data.get('content', ''))
        chunk_count += 1
    
    if chunk_count != total_chunks:
        print(f"    ⚠️ Warning: Expected {total_chunks} chunks, found {chunk_count}")
    
    full_content = "".join(chunks_data)
    print(f"    ✅ Reassembled {len(full_content):,} characters")
    
    # Return in the same format as regular files
    return {
        'original_path': metadata.get('original_path'),
        'content': full_content,
        'timestamp': metadata.get('timestamp'),
        'is_chunked': True,
        'total_size': len(full_content),
        'total_chunks': chunk_count
    }


@code_converter_bp.route('/code-converter/file/<project_id>/<doc_id>', methods=['PUT'])
def update_converted_file(project_id, doc_id):
    """
    Updates a file's content. For chunked files, automatically re-chunks if needed.
    """
    data = request.json
    new_content = data.get('content')
    
    if new_content is None:
        return jsonify({"error": "Missing 'content' in request body"}), 400
    
    try:
        doc_ref = db.collection(CODE_PROJECTS_COLLECTION).document(project_id).collection(CODE_FILES_SUBCOLLECTION).document(doc_id)
        doc = doc_ref.get()
        
        if not doc.exists:
            return jsonify({"error": "File not found"}), 404
        
        existing_data = doc.to_dict()
        new_hash = hashlib.sha256(new_content.encode('utf-8')).hexdigest()
        content_size = len(new_content)
        
        # Determine if chunking is needed
        MAX_CHUNK_SIZE = 900_000
        
        if content_size <= MAX_CHUNK_SIZE:
            # Small enough - store as regular document
            doc_ref.update({
                'content': new_content,
                'hash': new_hash,
                'timestamp': firestore.SERVER_TIMESTAMP,
                'is_chunked': False,
                'total_size': content_size
            })
            print(f"  ✅ Updated {doc_id} as single document")
            
        else:
            # Too large - need to chunk
            print(f"  🔪 Content too large ({content_size:,} bytes). Re-chunking...")
            update_chunked_content(doc_ref, new_content, new_hash, existing_data)
        
        return jsonify({
            "success": True,
            "message": "File updated successfully.",
            "size": content_size,
            "is_chunked": content_size > MAX_CHUNK_SIZE
        })
        
    except Exception as e:
        import traceback
        print(f"  ❌ Error updating file: {e}")
        print(traceback.format_exc())
        return jsonify({"error": str(e)}), 500


def update_chunked_content(doc_ref, new_content, new_hash, existing_data):
    """
    Updates a chunked document by re-chunking the new content.
    """
    MAX_CHUNK_SIZE = 900_000
    
    # Delete old chunks
    chunks_subcollection = doc_ref.collection('chunks')
    for chunk_doc in chunks_subcollection.stream():
        chunk_doc.reference.delete()
    
    # Create new chunks
    chunks = []
    current_pos = 0
    chunk_num = 0
    
    while current_pos < len(new_content):
        chunk_end = min(current_pos + MAX_CHUNK_SIZE, len(new_content))
        chunk_data = new_content[current_pos:chunk_end]
        
        chunks_subcollection.document(f'chunk_{chunk_num}').set({
            'order': chunk_num,
            'content': chunk_data,
            'size': len(chunk_data),
            'start_pos': current_pos,
            'end_pos': chunk_end
        })
        
        current_pos = chunk_end
        chunk_num += 1
    
    # Update metadata
    doc_ref.update({
        'hash': new_hash,
        'timestamp': firestore.SERVER_TIMESTAMP,
        'is_chunked': True,
        'total_size': len(new_content),
        'total_chunks': chunk_num,
        'chunk_size': MAX_CHUNK_SIZE
    })
    
    print(f"  ✅ Updated {doc_ref.id} with {chunk_num} chunks")


@code_converter_bp.route('/code-converter/file/<project_id>/<doc_id>', methods=['DELETE'])
def delete_converted_file(project_id, doc_id):
    """
    Deletes a file, including all chunks if it's a chunked document.
    """
    try:
        doc_ref = db.collection(CODE_PROJECTS_COLLECTION).document(project_id).collection(CODE_FILES_SUBCOLLECTION).document(doc_id)
        doc = doc_ref.get()
        
        if not doc.exists:
            return jsonify({"error": "File not found"}), 404
        
        data = doc.to_dict()
        
        # If chunked, delete all chunks first
        if data.get('is_chunked', False):
            print(f"  🗑️ Deleting chunks for {doc_id}...")
            chunks_ref = doc_ref.collection('chunks')
            for chunk_doc in chunks_ref.stream():
                chunk_doc.reference.delete()
        
        # Delete the main document
        doc_ref.delete()
        print(f"  ✅ Deleted {doc_id}")
        
        return jsonify({"success": True, "message": "File deleted successfully."}), 200
        
    except Exception as e:
        import traceback
        print(f"  ❌ Error deleting file: {e}")
        print(traceback.format_exc())
        return jsonify({"error": str(e)}), 500