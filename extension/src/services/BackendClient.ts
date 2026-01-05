import axios, { AxiosInstance } from 'axios';
import * as fs from 'fs';
import FormData from 'form-data'; 
import * as path from 'path';
import * as vscode from 'vscode';

// Define the ports for your local servers
const CPP_BACKEND_URL = 'http://localhost:5002';
const PYTHON_BACKEND_URL = 'http://localhost:5000';

// Create separate axios instances for each backend
const cppClient = axios.create({ baseURL: CPP_BACKEND_URL });
const pythonClient = axios.create({ baseURL: PYTHON_BACKEND_URL });

export class BackendClient {

    // --- System Health ---
    async checkAllBackends(): Promise<{ cpp: boolean, python: boolean }> {
        const check = async (client: AxiosInstance, name: string) => {
            try {
                const response = await client.get('/api/hello', { timeout: 1000 });
                return response.status === 200;
            } catch (error) {
                // console.warn(`[BackendClient] ${name} backend is offline.`);
                return false;
            }
        };
        const [cppStatus, pythonStatus] = await Promise.all([
            check(cppClient, 'C++'),
            check(pythonClient, 'Python'),
        ]);
        return { cpp: cppStatus, python: pythonStatus };
    }

    async getAutocomplete(prefix: string, suffix: string, projectId: string, filePath: string): Promise<string> {
        try {
            const response = await cppClient.post('/complete', { 
                prefix, 
                suffix,
                project_id: projectId,
                file_path: filePath // 🚀 NEW
            });
            return response.data.completion || "";
        } catch (error) {
            return "";
        }
    }

    async registerCodeProject(
        projectId: string, 
        workspacePath: string, 
        extensions: string[], 
        ignoredPaths: string[], 
        includedPaths: string[]
    ) {
        const filterConfig = {
            local_path: workspacePath,
            allowed_extensions: extensions,
            // Aligned with C++ SyncService FilterConfig spec
            ignored_paths: ignoredPaths,
            included_paths: includedPaths
        };

        return await cppClient.post(`/sync/register/${projectId}`, filterConfig);
    }

    async syncCodeProject(projectId: string, workspacePath: string): Promise<any> {
        const storagePath = path.join(workspacePath, '.study_assistant');
        
        const response = await cppClient.post(`/sync/run/${projectId}`, {
            storage_path: storagePath 
        });
        return response.data;
    }

    async getCodeSuggestion(projectId: string, prompt: string, activeContext?: any): Promise<string> {
        const response = await cppClient.post('/generate-code-suggestion', {
            project_id: projectId,
            prompt: prompt,
            active_file_path: activeContext?.filePath || "",
            active_file_content: activeContext?.content || "",
            active_selection: activeContext?.selection || ""
        });
        return response.data.suggestion;
    }
    
    async getContextCandidates(projectId: string, prompt: string): Promise<any[]> {
        const response = await cppClient.post('/retrieve-context-candidates', {
            project_id: projectId,
            prompt,
        });
        return response.data.candidates;
    }


    // --- Python Backend Methods ---
    async getStudyProjects(): Promise<{ id: string, name: string }[]> {
        const response = await pythonClient.get('/get-projects');
        return response.data;
    }

    async createStudyProject(name: string): Promise<string> {
        const response = await pythonClient.post('/create-project', { name });
        return response.data.id;
    }

    async getProjectSources(projectId: string): Promise<any[]> {
        const response = await pythonClient.get(`/get-sources/${projectId}`);
        return response.data;
    }

    async uploadPDF(projectId: string, filePath: string): Promise<void> {
        const form = new FormData();
        form.append('pdfs', fs.createReadStream(filePath));

        await pythonClient.post(`/upload-source/${projectId}`, form, {
            headers: form.getHeaders(),
        });
    }

    async getNote(projectId: string, sourceId: string): Promise<string> {
        const response = await pythonClient.get(`/get-note/${projectId}/${sourceId}`);
        return response.data.note_html;
    }

    async getPastPapers(projectId: string): Promise<any[]> {
        const response = await pythonClient.get(`/get-papers/${projectId}`);
        return response.data;
    }

    async uploadPastPaper(projectId: string, filePath: string, mode: 'multimodal' | 'text_only'): Promise<void> {
        const form = new FormData();
        form.append('paper', fs.createReadStream(filePath));
        form.append('analysis_mode', mode);

        await pythonClient.post(`/upload-paper/${projectId}`, form, {
            headers: form.getHeaders(),
        });
    }

    async syncSingleFile(projectId: string, relativePath: string): Promise<void> {
        await cppClient.post(`/sync/file/${projectId}`, {
            file_path: relativePath
        });
    }
}

// Singleton instance
let _client: BackendClient | null = null;
export function getBackendClient(): BackendClient {
    if (!_client) {
        _client = new BackendClient();
    }
    return _client;
}