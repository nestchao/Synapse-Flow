import * as vscode from 'vscode';
import { BackendClient } from '../services/BackendClient';

// 🚀 CLASS: SMART DEBOUNCER
class SmartDebouncer {
    private timer?: NodeJS.Timeout;
    private lastPrefix = "";
    private readonly DEBOUNCE_MS = 350; // Tuned for typing speed
    
    trigger(prefix: string, callback: () => void) {
        if (this.timer) clearTimeout(this.timer);
        
        // Anti-Jitter: Don't fire if only 1 char changed and it's alphanumeric
        // (Let user finish the word)
        if (Math.abs(prefix.length - this.lastPrefix.length) < 2 && /[\w]$/.test(prefix)) {
             // Optional: Increase debounce for mid-word typing
        }

        this.timer = setTimeout(() => {
            this.lastPrefix = prefix;
            callback();
        }, this.DEBOUNCE_MS);
    }
}

// 🚀 CLASS: LOCAL PREDICTOR (0ms Latency)
class InstantPredictor {
    predict(prefix: string): vscode.InlineCompletionItem[] {
        const p = prefix.trimEnd();
        // Common C++ Patterns
        if (p.endsWith("if (")) return [new vscode.InlineCompletionItem("condition) {\n\t\n}")];
        if (p.endsWith("for (")) return [new vscode.InlineCompletionItem("int i = 0; i < count; ++i) {\n\t\n}")];
        if (p.endsWith("class ")) return [new vscode.InlineCompletionItem("Name {\npublic:\n\tName();\n\t~Name();\n};")];
        if (p.endsWith("#include <")) return [new vscode.InlineCompletionItem("iostream>")];
        if (p.endsWith("std::")) return [new vscode.InlineCompletionItem("vector<T> v;")];
        return [];
    }
}

export class GhostTextProvider implements vscode.InlineCompletionItemProvider {
    private debouncer = new SmartDebouncer();
    private predictor = new InstantPredictor();

    constructor(
        private readonly _backendClient: BackendClient,
        private readonly _getProjectId: () => string | null 
    ) {}

    async provideInlineCompletionItems(
        document: vscode.TextDocument,
        position: vscode.Position,
        context: vscode.InlineCompletionContext,
        token: vscode.CancellationToken
    ): Promise<vscode.InlineCompletionItem[]> {

        const projectId = this._getProjectId(); 
        if (!projectId) return [];

        const prefix = document.getText(new vscode.Range(new vscode.Position(Math.max(0, position.line - 20), 0), position));
        const suffix = document.getText(new vscode.Range(position, new vscode.Position(position.line + 10, 0)));

        // 🚀 PHASE 1: INSTANT LOCAL PREDICTION
        const localPreds = this.predictor.predict(prefix);
        if (localPreds.length > 0) {
            console.log("⚡ [Ghost] Served Local Prediction");
            return localPreds;
        }

        // 🚀 PHASE 2: AI PREDICTION (DEBOUNCED)
        return new Promise<vscode.InlineCompletionItem[]>((resolve) => {
            this.debouncer.trigger(prefix, async () => {
                if (token.isCancellationRequested) { resolve([]); return; }

                console.log(`📡 [Ghost] Dispatching AI Request...`);
                try {
                    // We send the current file path to help the backend exclude it from RAG if needed
                    const currentFile = vscode.workspace.asRelativePath(document.uri);
                    
                    const result = await this._backendClient.getAutocomplete(prefix, suffix, projectId, currentFile);
                    
                    if (!result || result.trim() === "") { resolve([]); return; }

                    const item = new vscode.InlineCompletionItem(result);
                    item.range = new vscode.Range(position, position);
                    resolve([item]);
                } catch (e) {
                    resolve([]);
                }
            });
        });
    }
}