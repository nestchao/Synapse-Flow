(function () {
    const vscode = acquireVsCodeApi();
    const chatContainer = document.getElementById('chat-container');
    const promptInput = document.getElementById('prompt');
    const sendBtn = document.getElementById('send-btn');

    // --- MARKDOWN RENDERER CONFIG ---
    const renderer = new marked.Renderer();
    renderer.code = function (tokenOrCode, language) {
        let codeText = typeof tokenOrCode === 'object' ? tokenOrCode.text : tokenOrCode;
        let lang = typeof tokenOrCode === 'object' ? (tokenOrCode.lang || "") : (language || "");
        
        const id = 'code-' + Math.random().toString(36).substr(2, 9);
        // Clean comments like [TARGET:file.ts]
        const displayCode = codeText.replace(/(?:\/\/|#|--)\s*\[TARGET:.*?\]\s*\n?/, "");
        
        return `
            <div class="code-block-container" id="${id}">
                <div class="code-header">
                    <span>${lang || 'PLAINTEXT'}</span>
                    <div class="code-actions">
                        <button class="action-btn accept-btn" data-action="accept" data-block-id="${id}">Inject</button>
                        <button class="action-btn reject-btn" data-action="reject" data-block-id="${id}">Discard</button>
                    </div>
                </div>
                <pre><code class="language-${lang}">${displayCode}</code><div style="display:none" class="hidden-raw">${codeText}</div></pre>
            </div>`;
    }
    marked.setOptions({ renderer: renderer, gfm: true, breaks: true });

    // --- AUTO RESIZE TEXTAREA ---
    promptInput.addEventListener('input', function() {
        this.style.height = 'auto';
        this.style.height = (this.scrollHeight) + 'px';
        if(this.value === '') this.style.height = 'auto';
    });

    // --- STATE MANAGEMENT ---
    const state = vscode.getState() || { messages: [] };
    
    function saveState() {
        // Limit state to last 50 messages to prevent memory issues
        const messages = Array.from(chatContainer.children).slice(-50).map(child => child.outerHTML);
        vscode.setState({ messages });
    }

    if (state.messages && state.messages.length > 0) {
        chatContainer.innerHTML = state.messages.join('');
        // Re-scroll to bottom
        setTimeout(() => chatContainer.scrollTop = chatContainer.scrollHeight, 100);
    }

    // --- EVENT LISTENERS ---
    chatContainer.addEventListener('click', (e) => {
        const target = e.target;
        if (target.classList.contains('action-btn')) {
            const action = target.getAttribute('data-action');
            const blockId = target.getAttribute('data-block-id');
            const container = document.getElementById(blockId);
            
            if (action === 'accept') {
                const rawCode = container.querySelector('.hidden-raw').innerText;
                // Visual Feedback
                target.innerText = "INJECTING...";
                target.style.background = "#20c997";
                vscode.postMessage({ type: 'applyCode', value: rawCode, id: blockId });
            } else if (action === 'reject') {
                container.style.opacity = '0.5';
                container.style.filter = 'grayscale(1)';
                container.querySelector('.code-actions').innerHTML = '<span style="color:#adb5bd; font-size:10px;">DISCARDED</span>';
                saveState();
            }
        }
    });

    function handleSend() {
        const text = promptInput.value.trim();  
        if (!text) return;
        
        // 1. Add User Message
        const div = document.createElement('div');
        div.className = 'message user';
        div.innerText = text;
        chatContainer.appendChild(div);
        
        // 2. Add Thinking Placeholder
        const thinkingDiv = document.createElement('div');
        thinkingDiv.className = 'message bot thinking';
        thinkingDiv.id = 'temp-thinking';
        thinkingDiv.innerText = 'Chanting spell...';
        chatContainer.appendChild(thinkingDiv);

        // Send to Extension
        vscode.postMessage({ type: 'askCode', value: text });
        
        // Reset Input
        promptInput.value = '';
        promptInput.style.height = 'auto';
        chatContainer.scrollTop = chatContainer.scrollHeight;
        saveState();
    }

    sendBtn.addEventListener('click', handleSend);
    promptInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' && !e.shiftKey) {
            e.preventDefault();
            handleSend();
        }
    });

    // --- MESSAGE HANDLER ---
    window.addEventListener('message', event => {
        const message = event.data;
        
        // Remove temp thinking bubble if it exists
        const tempThinking = document.getElementById('temp-thinking');
        if (tempThinking) tempThinking.remove();

        const bots = chatContainer.querySelectorAll('.message.bot');
        const lastBot = bots[bots.length - 1];

        switch (message.type) {
            case 'addResponse':
                // For "Thinking..." updates from backend
                const div = document.createElement('div');
                div.className = message.value.includes('Thinking') ? 'message bot thinking' : 'message bot';
                div.innerHTML = marked.parse(message.value); 
                chatContainer.appendChild(div);
                saveState();
                break;
                
            case 'updateLastResponse':
                // The main AI response replaces the last bubble (usually the thinking one)
                let content = message.value;
                // Auto-wrap raw code if it looks like code but isn't marked
                if ((content.includes("def ") || content.includes("class ") || content.includes("import ")) && !content.includes("```")) {
                    content = "```python\n" + content + "\n```";
                }
                
                // If there was no last bot bubble (rare), create one
                if (!lastBot) {
                    const newDiv = document.createElement('div');
                    newDiv.className = 'message bot';
                    newDiv.innerHTML = marked.parse(content);
                    chatContainer.appendChild(newDiv);
                } else {
                    lastBot.className = 'message bot'; // Remove 'thinking' class
                    lastBot.innerHTML = marked.parse(content);
                }
                saveState();
                break;
                
            case 'applySuccess':
                const block = document.getElementById(message.id);
                if (block) {
                    block.style.borderColor = "#20c997";
                    block.style.boxShadow = "0 0 15px rgba(32, 201, 151, 0.4)";
                    const actions = block.querySelector('.code-actions');
                    actions.innerHTML = '<span style="color:#20c997; font-weight:bold;">★ SPELL CAST (APPLIED)</span>';
                    saveState();
                }
                break;
        }
        chatContainer.scrollTop = chatContainer.scrollHeight;
    });
}());