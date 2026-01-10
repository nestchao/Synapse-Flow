import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import { BackendClient } from '../services/BackendClient';

export class CodeChatProvider implements vscode.WebviewViewProvider {
    private _view?: vscode.WebviewView;
    private _sessionId: string;

    constructor(
        private readonly _extensionUri: vscode.Uri,
        private readonly _backendClient: any,
        private readonly _projectId: string | null
    ) {
        this._sessionId = "VSCODE_" + Math.random().toString(36).substring(2, 15) + Math.random().toString(36).substring(2, 15);
    }

    public resolveWebviewView(
        webviewView: vscode.WebviewView,
        context: vscode.WebviewViewResolveContext,
        _token: vscode.CancellationToken,
    ) {
        this._view = webviewView;

        webviewView.webview.options = {
            enableScripts: true,
            // 🚀 CRITICAL: Allow access to the media folder
            localResourceRoots: [
                vscode.Uri.joinPath(this._extensionUri, 'media'),
                vscode.Uri.joinPath(this._extensionUri, 'node_modules')
            ]
        };

        webviewView.webview.html = this._getHtmlForWebview(webviewView.webview);

        webviewView.webview.onDidReceiveMessage(async (data) => {
            switch (data.type) {
                case 'askCode': {
                    if (!data.value || !this._projectId) {
                        this._view?.webview.postMessage({
                            type: 'addResponse',
                            value: '⚠️ Project not registered. Please use the Config panel to register this folder.'
                        });
                        return;
                    }

                    // 1. Immediate Feedback
                    // Note: The C++ backend is fast, but "Thinking" lets the user know complex plans are forming.
                    this._view?.webview.postMessage({ type: 'addResponse', value: '🧠 Thinking...' });

                    try {
                        // 2. Gather Context
                        const activeEditor = vscode.window.activeTextEditor;
                        const activeContext = activeEditor ? {
                            filePath: vscode.workspace.asRelativePath(activeEditor.document.uri),
                            content: activeEditor.document.getText(),
                            selection: activeEditor.document.getText(activeEditor.selection)
                        } : { filePath: "None", content: "", selection: "" };

                        // 3. Send to C++ Brain
                        console.log(`[Extension] Sending to Agent [Session: ${this._sessionId}]`);
                        
                        const response = await this._backendClient.getCodeSuggestion(
                            this._projectId,
                            data.value,
                            this._sessionId, // 🚀 Persistent Graph Session
                            activeContext
                        );

                        // 4. Update UI
                        // Removes "Thinking..." and adds real response
                        this._view?.webview.postMessage({
                            type: 'updateLastResponse',
                            value: response
                        });
                    } catch (error: any) {
                        this._view?.webview.postMessage({
                            type: 'updateLastResponse',
                            value: `❌ Communication Error: ${error.message}`
                        });
                    }
                    break;
                }

                case 'applyCode': {
                    // (Keep existing Apply Code logic - unchanged)
                    this.handleApplyCode(data.value, data.id);
                    break;
                }
            }
        });

        webviewView.onDidDispose(() => { this._view = undefined; });
    }

    private async handleApplyCode(rawCode: string, blockId: string) {
        
        const workspaceFolders = vscode.workspace.workspaceFolders;
        if (!workspaceFolders) return;

        try {
            const headerMatch = rawCode.match(/(?:\/\/|#|--)\s*\[TARGET:\s*([^:\]\s]+)(?::([^:\]\s]+))?(?::([\s\S]*?))?\]/i);
            
            let relativePath = "";
            let action = "INSERT";
            let searchKey = "";

            if (headerMatch) {
                relativePath = headerMatch[1].trim();
                action = (headerMatch[2] || "INSERT").toUpperCase();
                searchKey = headerMatch[3] ? headerMatch[3].trim() : "";
            } else {
                // Fallback if no header (assume active editor)
                const activeEditor = vscode.window.activeTextEditor;
                if(activeEditor) {
                    relativePath = vscode.workspace.asRelativePath(activeEditor.document.uri);
                } else {
                    throw new Error("No target file specified and no active editor.");
                }
            }

            // Cleanup code (remove the tag line)
            const cleanCode = rawCode.replace(/(?:\/\/|#|--)\s*\[TARGET:.*?\]\s*\n?/, "").trim();
            
            const targetUri = vscode.Uri.joinPath(workspaceFolders[0].uri, relativePath);
            const document = await vscode.workspace.openTextDocument(targetUri);
            const edit = new vscode.WorkspaceEdit();
            const fullText = document.getText();

            // Simple Logic for now (Expand based on your previous ApplyCode logic)
            if(action === "REPLACE" && searchKey) {
                const idx = fullText.indexOf(searchKey);
                if(idx !== -1) {
                    const startPos = document.positionAt(idx);
                    const endPos = document.positionAt(idx + searchKey.length);
                    edit.replace(targetUri, new vscode.Range(startPos, endPos), cleanCode);
                } else {
                    throw new Error("Search key not found for replacement.");
                }
            } else {
                // Default: Append or Insert at end
                const lastLine = document.lineAt(document.lineCount - 1);
                edit.insert(targetUri, lastLine.range.end, "\n" + cleanCode);
            }

            await vscode.workspace.applyEdit(edit);
            await document.save();
            
            this._view?.webview.postMessage({ type: 'applySuccess', id: blockId });

        } catch (err: any) {
            vscode.window.showErrorMessage(`Apply Failed: ${err.message}`);
        }
    }

    private _getHtmlForWebview(webview: vscode.Webview) {
        // 🚀 RESOLVER: Map absolute paths
        const scriptUri = webview.asWebviewUri(vscode.Uri.joinPath(this._extensionUri, 'media', 'chat.js'));
        const styleUri = webview.asWebviewUri(vscode.Uri.joinPath(this._extensionUri, 'media', 'chat.css'));
        // Use a CDN link that is most likely to pass CSP, but add it to the policy
        const markedUri = "https://cdn.jsdelivr.net/npm/marked/marked.min.js";

        console.log("🚀 [Host] Injecting Script URI:", scriptUri.toString());
        console.log("🚀 [Host] Injecting Style URI:", styleUri.toString());

        return `<!DOCTYPE html>
        <html lang="en">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <!-- 🚀 CSP: Explicitly allow the scripts and styles -->
            <meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src ${webview.cspSource} https:; script-src ${webview.cspSource} 'unsafe-inline' https://cdn.jsdelivr.net; style-src ${webview.cspSource} 'unsafe-inline';">
            <link href="${styleUri}" rel="stylesheet">
        </head>
        <body>
            <div id="chat-container"></div>
            <div class="input-wrapper">
                <div class="input-container">
                    <textarea id="prompt" rows="1" placeholder="Ask anything..."></textarea>
                    <button id="send-btn">
                        <svg viewBox="0 0 24 24"><path d="M2.01 21L23 12 2.01 3 2 10l15 2-15 2z"/></svg>
                    </button>
                </div>
            </div>
            <!-- Load dependencies -->
            <script src="${markedUri}"></script>
            <script src="${scriptUri}"></script>
        </body>
        </html>`;
    }
}