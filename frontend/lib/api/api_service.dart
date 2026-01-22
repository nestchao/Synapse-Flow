// lib/api/api_service.dart
import 'dart:convert';
import 'package:http/http.dart' as http;
import 'package:file_picker/file_picker.dart';
import 'package:flutter_dotenv/flutter_dotenv.dart';
import 'package:flutter/foundation.dart' show kIsWeb;
import 'dart:typed_data';
import 'package:study_assistance/models/dependency_graph.dart';
import 'dart:io' show Platform;

class ApiService {
  late final String baseUrl;

  ApiService() {
    if (kIsWeb) {
      // Browser logic
      baseUrl = 'http://localhost:5000';
    } else if (Platform.isWindows) {
      // Windows Native logic: MUST use localhost or 127.0.0.1
      baseUrl = 'http://127.0.0.1:5000';
    } else {
      // Android Emulator logic
      baseUrl = dotenv.env['API_URL'] ?? 'http://10.0.2.2:5000';
    }
    print("API Base URL initialized as: $baseUrl");
  }

  // --- MODIFIED: SYNC SERVICE METHODS ---
  Future<List<Map<String, dynamic>>> getSyncProjects() async {
    final response = await http.get(Uri.parse('$baseUrl/sync/projects'));
    if (response.statusCode == 200) {
      return List<Map<String, dynamic>>.from(json.decode(response.body));
    }
    throw Exception('Failed to load sync projects');
  }

  Future<void> registerFolderToProject(
    String projectId,
    String path,
    List<String> extensions,
    List<String> ignoredPaths,
    List<String> includedPaths, // New
    String syncMode, // New
  ) async {
    final response = await http.post(
      Uri.parse('$baseUrl/sync/register/$projectId'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({
        'local_path': path,
        'extensions': extensions,
        'ignored_paths': ignoredPaths,
        'included_paths': includedPaths, // New
        'sync_mode': syncMode, // New
      }),
    );
    if (response.statusCode != 200) {
      throw Exception('Failed to register folder to project');
    }
  }

  Future<void> updateSyncProject(
    String projectId, {
    String? name, // <--- 1. ADD THIS ARGUMENT
    bool? isActive,
    List<String>? extensions,
    List<String>? ignoredPaths,
    List<String>? includedPaths,
    String? syncMode,
  }) async {
    final Map<String, dynamic> body = {};
    if (name != null) body['name'] = name; // <--- 2. ADD THIS LINE
    if (isActive != null) body['is_active'] = isActive;
    if (extensions != null) body['allowed_extensions'] = extensions;
    if (ignoredPaths != null) body['ignored_paths'] = ignoredPaths;
    if (includedPaths != null) body['included_paths'] = includedPaths;
    if (syncMode != null) body['sync_mode'] = syncMode;

    final response = await http.put(
      Uri.parse('$baseUrl/sync/project/$projectId'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode(body),
    );
    if (response.statusCode != 200) {
      throw Exception('Failed to update sync project');
    }
  }

  Future<void> deleteSyncFromProject(String projectId) async {
    final response =
        await http.delete(Uri.parse('$baseUrl/sync/project/$projectId'));
    if (response.statusCode != 200) {
      throw Exception('Failed to delete sync from project');
    }
  }

  // GET /api/get-projects
  Future<List<Map<String, dynamic>>> getProjects() async {
    final response = await http.get(Uri.parse('$baseUrl/get-projects'));
    return List<Map<String, dynamic>>.from(json.decode(response.body));
  }

  Future<String?> createStudyProjectAndGetId(String name) async {
    final response = await http.post(
      Uri.parse('$baseUrl/create-project'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({'name': name}),
    );
    if (response.statusCode == 200 || response.statusCode == 201) {
      return json.decode(response.body)['id'];
    }
    throw Exception('Failed to create study project: ${response.statusCode}');
  }

  Future<List<Map<String, dynamic>>> getCodeProjects() async {
    final response = await http.get(Uri.parse('$baseUrl/get-code-projects'));
    if (response.statusCode == 200) {
      return List<Map<String, dynamic>>.from(json.decode(response.body));
    }
    throw Exception('Failed to load code projects');
  }

  Future<String?> createCodeProjectAndGetId(String name) async {
    final response = await http.post(
      Uri.parse('$baseUrl/create-code-project'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({'name': name}),
    );
    if (response.statusCode == 200 || response.statusCode == 201) {
      return json.decode(response.body)['id'];
    }
    throw Exception('Failed to create code project: ${response.statusCode}');
  }

  Future<void> renameProject(String projectId, String newName) async {
    final response = await http.put(
      Uri.parse('$baseUrl/rename-project/$projectId'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({'new_name': newName}),
    );
    if (response.statusCode != 200) throw Exception('Failed to rename project');
  }

  Future<void> deleteProject(String projectId) async {
    final response =
        await http.delete(Uri.parse('$baseUrl/delete-project/$projectId'));
    if (response.statusCode != 200) throw Exception('Failed to delete project');
  }

  Future<void> createProject(String name) async {
    await http.post(
      Uri.parse('$baseUrl/create-project'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({'name': name}),
    );
  }

  Future<void> deleteSource(String projectId, String sourceId) async {
    final response = await http
        .delete(Uri.parse('$baseUrl/delete-source/$projectId/$sourceId'));
    if (response.statusCode != 200) throw Exception('Failed to delete source');
  }

  // GET /api/get-sources/<project_id>
  Future<List<Map<String, dynamic>>> getSources(String projectId) async {
    final response =
        await http.get(Uri.parse('$baseUrl/get-sources/$projectId'));
    return List<Map<String, dynamic>>.from(json.decode(response.body));
  }

  // POST /api/upload-source/<project_id>
  Future<void> uploadSources(String projectId, List<PlatformFile> files) async {
    print("📤 Uploading ${files.length} files to project $projectId");

    var request = http.MultipartRequest(
      'POST',
      Uri.parse('$baseUrl/upload-source/$projectId'),
    );

    for (var file in files) {
      print("  Processing file: ${file.name} (Size: ${file.size})");

      // FIX: Handle both Path (Desktop/Mobile) and Bytes (Web)
      if (kIsWeb) {
        // Web always provides bytes
        if (file.bytes != null) {
          request.files.add(http.MultipartFile.fromBytes(
            'pdfs',
            file.bytes!,
            filename: file.name,
          ));
          print("    -> Added from bytes (Web)");
        }
      } else {
        // Mobile/Desktop usually provides path
        if (file.path != null) {
          request.files.add(await http.MultipartFile.fromPath(
            'pdfs',
            file.path!,
            filename: file.name,
          ));
          print("    -> Added from path: ${file.path}");
        } else if (file.bytes != null) {
          // Fallback if path is null but bytes exist
          request.files.add(http.MultipartFile.fromBytes(
            'pdfs',
            file.bytes!,
            filename: file.name,
          ));
          print("    -> Added from bytes (Fallback)");
        } else {
          print("    ⚠️ SKIPPED: File has no path and no bytes.");
        }
      }
    }

    // Check if files were actually added
    if (request.files.isEmpty) {
      throw Exception('No files could be read. Please try again.');
    }

    print("  Sending request with ${request.files.length} parts...");
    var response = await request.send();
    var responseBody = await response.stream.bytesToString();

    print("  Response status: ${response.statusCode}");
    // print("  Response body: $responseBody"); // Uncomment for detailed debug

    if (response.statusCode != 200) {
      throw Exception('Upload failed: $responseBody');
    }
  }

  // GET /api/get-note/<project_id>/<source_id>
  Future<String> getNote(String projectId, String sourceId) async {
    final response =
        await http.get(Uri.parse('$baseUrl/get-note/$projectId/$sourceId'));
    return json.decode(response.body)['note_html'] ?? '<p>No note</p>';
  }

  // POST /api/generate-topic-note/<project_id>
  Future<String> generateTopicNote(String projectId, String topic) async {
    final response = await http.post(
      Uri.parse('$baseUrl/generate-topic-note/$projectId'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({'topic': topic}),
    );
    return json.decode(response.body)['note_html'] ?? '<p>Failed</p>';
  }

  // POST /api/ask-chatbot/<project_id>
  Future<String> askChatbot(String projectId, String question,
      {String? sourceId, List<Map<String, dynamic>>? history}) async {
    final response = await http.post(
      Uri.parse('$baseUrl/ask-chatbot/$projectId'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({
        'question': question,
        'source_id': sourceId,
        'history': history ?? [],
      }),
    );
    return json.decode(response.body)['answer'] ?? 'No response';
  }

  // GET /api/hello
  Future<Map<String, dynamic>> hello() async {
    final response = await http.get(Uri.parse('$baseUrl/api/hello'));
    if (response.statusCode == 200) {
      return json.decode(response.body);
    } else {
      return {"message": "Backend offline"};
    }
  }

  Future<String?> uploadImageBytes(
      String projectId, Uint8List imageBytes, String fileName) async {
    final String base64Image = base64Encode(imageBytes);

    final response = await http.post(
      Uri.parse('$baseUrl/media/upload'),
      headers: {
        'Content-Type': 'application/json',
        'X-User-ID': projectId, // Using projectId as a stand-in for user ID
      },
      body: json.encode({
        'content': base64Image,
        'fileName': fileName,
        'type': 'image',
      }),
    );

    if (response.statusCode == 201) {
      return json.decode(response.body)['mediaId'];
    }
    return null;
  }

  Future<Uint8List?> getMediaBytes(String mediaId, String projectId) async {
    final response = await http.get(
      Uri.parse('$baseUrl/media/get/$mediaId'),
      headers: {'X-User-ID': projectId},
    );

    if (response.statusCode == 200) {
      // THIS IS CORRECT: Return the raw bytes of the response body.
      return response.bodyBytes;
    }

    print("Failed to get media bytes: ${response.statusCode}");
    return null;
  }

  Future<bool> updateNote(
      String projectId, String sourceId, String newHtmlContent) async {
    final response = await http.post(
      Uri.parse('$baseUrl/update-note/$projectId/$sourceId'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({'html_content': newHtmlContent}),
    );

    if (response.statusCode == 200) {
      return true;
    } else {
      print("Failed to update note: ${response.body}");
      return false;
    }
  }

  // --- NEW: Get Past Papers ---
  Future<List<Map<String, dynamic>>> getPastPapers(String projectId) async {
    final response =
        await http.get(Uri.parse('$baseUrl/get-papers/$projectId'));
    if (response.statusCode == 200) {
      // --- MODIFY THIS PART ---
      try {
        return List<Map<String, dynamic>>.from(json.decode(response.body));
      } catch (e) {
        throw Exception('Failed to parse server response.');
      }
    } else {
      throw Exception('Failed to load past papers');
    }
  }

  // --- NEW: Upload Past Paper ---
  Future<Map<String, dynamic>> uploadPastPaper(
  String projectId,
  PlatformFile file,
  String analysisMode,
) async {
  var request = http.MultipartRequest(
    'POST',
    Uri.parse('$baseUrl/upload-paper/$projectId'),
  );

  request.fields['analysis_mode'] = analysisMode;

  // HANDLE PATH (Desktop/Mobile) vs BYTES (Web)
  if (kIsWeb) {
    if (file.bytes == null) throw Exception('No file data found');
    request.files.add(http.MultipartFile.fromBytes(
      'paper',
      file.bytes!,
      filename: file.name,
    ));
  } else {
    // On Windows/Android/iOS, use the file path
    if (file.path == null) throw Exception('File path not found');
    request.files.add(await http.MultipartFile.fromPath(
      'paper',
      file.path!,
      filename: file.name,
    ));
  }

  var response = await request.send();
  var responseBody = await response.stream.bytesToString();
  
  if (response.statusCode == 200) {
    return json.decode(responseBody);
  } else {
    final decoded = json.decode(responseBody);
    throw Exception(decoded['error'] ?? 'Failed to upload paper');
  }
}

  Future<void> deletePastPaper(String projectId, String paperId) async {
    final response = await http.delete(
      Uri.parse('$baseUrl/delete-paper/$projectId/$paperId'),
    );

    if (response.statusCode != 200) {
      final error =
          json.decode(response.body)['error'] ?? 'Failed to delete past paper';
      throw Exception(error);
    }
  }

  Future<Map<String, dynamic>> runSync(String projectId) async {
    final response = await http.post(Uri.parse('$baseUrl/sync/run/$projectId'));
    if (response.statusCode == 200) {
      return json.decode(response.body);
    }
    final error = json.decode(response.body)['error'] ?? 'Failed to run sync';
    throw Exception(error);
  }

  // --- CODE CONVERTER METHODS ---

  Future<Map<String, dynamic>> getProjectFileStructure(String projectId) async {
    final response = await http
        .get(Uri.parse('$baseUrl/code-converter/structure/$projectId'));
    if (response.statusCode == 200) {
      return json.decode(response.body);
    }
    throw Exception('Failed to get project structure');
  }

  Future<String> getFileContent(String projectId, String docId) async {
    // Call the new, correct endpoint
    final response = await http
        .get(Uri.parse('$baseUrl/code-converter/file/$projectId/$docId'));

    if (response.statusCode == 200) {
      // The backend returns JSON: {"content": "...", "original_path": "..."}
      final data = json.decode(response.body);
      return data['content'] ?? 'Error: Content field missing in response.';
    }

    print(
        "Failed to get file content, Status: ${response.statusCode}, Body: ${response.body}");
    throw Exception('Failed to get file content');
  }

  Future<String> regenerateNote(String projectId, String sourceId) async {
    final response = await http.post(
      Uri.parse('$baseUrl/regenerate-note/$projectId/$sourceId'),
      headers: {'Content-Type': 'application/json'},
    );

    if (response.statusCode == 200) {
      final data = json.decode(response.body);
      return data['note_html'] ??
          '<p>Error: Regeneration failed to return content.</p>';
    } else {
      final errorData = json.decode(response.body);
      throw Exception(errorData['error'] ?? 'Failed to regenerate note');
    }
  }

  Future<String> getCodeSuggestion({
    required String projectId,
    required List<String> extensions,
    required String prompt,
  }) async {
    final response = await http.post(
      Uri.parse('$baseUrl/generate-code-suggestion'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({
        'project_id': projectId,
        'extensions': extensions,
        'prompt': prompt,
      }),
    );

    if (response.statusCode == 200) {
      return json.decode(response.body)['suggestion'] ?? 'No suggestion';
    } else {
      final error = json.decode(response.body)['error'] ?? 'Unknown error';
      throw Exception('Failed: $error');
    }
  }

  Future<String?> createProjectAndGetId(String name) async {
    final response = await http.post(
      Uri.parse('$baseUrl/create-project'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({'name': name}),
    );

    if (response.statusCode == 200 || response.statusCode == 201) {
      final data = json.decode(response.body);
      return data['id']; // The backend returns {"id": "project_id"}
    }

    throw Exception('Failed to create project: ${response.statusCode}');
  }

  Future<List<Map<String, dynamic>>> getRetrievalCandidates(
      String projectId, String prompt) async {
    final response = await http.post(
      Uri.parse('$baseUrl/retrieve-context-candidates'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({
        'project_id': projectId,
        'prompt': prompt,
      }),
    );

    if (response.statusCode == 200) {
      final data = json.decode(response.body);
      // Backend returns { "candidates": [...] }
      return List<Map<String, dynamic>>.from(data['candidates']);
    }
    throw Exception('Failed to retrieve candidates: ${response.body}');
  }

  Future<String> generateAnswerFromContext(
      String projectId, String prompt, List<String> selectedIds) async {
    final response = await http.post(
      Uri.parse('$baseUrl/generate-answer-from-context'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({
        'project_id': projectId,
        'prompt': prompt,
        'selected_ids': selectedIds,
      }),
    );

    if (response.statusCode == 200) {
      return json.decode(response.body)['suggestion'] ?? 'No response';
    }
    throw Exception('Failed to generate answer: ${response.body}');
  }

  Future<DependencyGraph> getDependencyGraph(
      String projectId, String nodeId) async {
    final response = await http.post(
      Uri.parse('$baseUrl/get-dependency-subgraph'),
      body: json.encode({'project_id': projectId, 'node_id': nodeId}),
    );
    if (response.statusCode == 200) {
      return DependencyGraph.fromJson(json.decode(response.body));
    }
    throw Exception("Failed to load graph");
  }

  Future<Map<String, dynamic>> getAvailableModelsWithState() async {
    try {
      final response = await http.get(Uri.parse('$baseUrl/bridge/get-models'));
      if (response.statusCode == 200) {
        // This returns the full Map: {"models": [...], "current_active": "..."}
        return json.decode(response.body);
      }
      print("Failed to load models: ${response.body}");
      return {"models": [], "current_active": null};
    } catch (e) {
      print("Error in getAvailableModelsWithState: $e");
      return {"models": [], "current_active": null};
    }
  }

  Future<bool> setModel(String modelName) async {
    final response = await http.post(
      Uri.parse('$baseUrl/bridge/set-model'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({'model_name': modelName}),
    );
    return response.statusCode == 200;
  }
}
