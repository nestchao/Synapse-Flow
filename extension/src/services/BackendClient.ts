import axios, { AxiosInstance } from 'axios';
import * as fs from 'fs';
import FormData from 'form-data'; 
import * as path from 'path';
import * as vscode from 'vscode';

const CPP_BACKEND_URL = 'http://localhost:5002';
const PYTHON_BACKEND_URL = 'http://localhost:5000';

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
                return false;
            }
        };
        const [cppStatus, pythonStatus] = await Promise.all([
            check(cppClient, 'C++'),
            check(pythonClient, 'Python'),
        ]);
        return { cpp: cppStatus, python: pythonStatus };
    }

    // --- Ghost Text ---
    async getAutocomplete(prefix: string, suffix: string, projectId: string, filePath: string): Promise<string> {
        try {
            const response = await cppClient.post('/complete', { 
                prefix, 
                suffix,
                project_id: projectId,
                file_path: filePath
            });
            return response.data.completion || "";
        } catch (error) {
            return "";
        }
    }

    // --- Configuration & Sync ---
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

    async syncSingleFile(projectId: string, relativePath: string): Promise<void> {
        await cppClient.post(`/sync/file/${projectId}`, {
            file_path: relativePath
        });
    }

    // --- 🚀 COGNITIVE AGENT INTERFACE ---
    async getCodeSuggestion(
        projectId: string, 
        prompt: string, 
        sessionId: string, 
        activeContext?: any
    ): Promise<string> {
        try {
            // ✅ FIX: Increase timeout to 300,000ms (5 minutes)
            // This accommodates the slower Python Browser Bridge
            const response = await cppClient.post('/generate-code-suggestion', {
                project_id: projectId,
                prompt: prompt,
                session_id: sessionId, 
                active_file_path: activeContext?.filePath || "",
                active_file_content: activeContext?.content || "",
                active_selection: activeContext?.selection || ""
            }, { timeout: 300000 }); 

            return response.data.suggestion;
        } catch (error: any) {
            console.error("Agent Error:", error);
            if (error.code === 'ECONNABORTED') {
                return "⚠️ The Agent timed out (Browser Bridge took too long).";
            }
            if (error.response?.status === 500) {
                return "❌ Internal Engine Error. Check the C++ terminal.";
            }
            return `Error: ${error.message}`;
        }
    }
    
    // --- Legacy / Python Features ---
    async getStudyProjects(): Promise<{ id: string, name: string }[]> {
        const response = await pythonClient.get('/get-projects');
        return response.data;
    }

    async createStudyProject(name: string): Promise<string> {
        const response = await pythonClient.post('/create-project', { name });
        return response.data.id;
    }

    async uploadPDF(projectId: string, filePath: string): Promise<void> {
        const form = new FormData();
        form.append('pdfs', fs.createReadStream(filePath));
        await pythonClient.post(`/upload-source/${projectId}`, form, {
            headers: form.getHeaders(),
        });
    }

    async uploadPastPaper(projectId: string, filePath: string, mode: 'multimodal' | 'text_only'): Promise<void> {
        const form = new FormData();
        form.append('paper', fs.createReadStream(filePath));
        form.append('analysis_mode', mode);
        await pythonClient.post(`/upload-paper/${projectId}`, form, {
            headers: form.getHeaders(),
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