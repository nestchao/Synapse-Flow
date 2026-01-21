(function () {
    const vscode = acquireVsCodeApi();
    const chatContainer = document.getElementById('chat-container');
    const promptInput = document.getElementById('prompt');
    const sendBtn = document.getElementById('send-btn');

    const renderer = new marked.Renderer();
    renderer.code = function (tokenOrCode, language) {
        let codeText = typeof tokenOrCode === 'object' ? tokenOrCode.text : tokenOrCode;
        let lang = typeof tokenOrCode === 'object' ? (tokenOrCode.lang || "") : (language || "");
        const id = 'code-' + Math.random().toString(36).substr(2, 9);
        const displayCode = codeText.replace(/(?:\/\/|#|--)\s*\[TARGET:.*?\]\s*\n?/, "");
        
        return `
            <div class="code-block-container" id="${id}">
                <div class="code-header">
                    <span>${lang || 'code'}</span>
                    <div class="code-actions">
                        <button class="action-btn accept-btn" data-action="accept" data-block-id="${id}">Accept & Write</button>
                        <button class="action-btn reject-btn" data-action="reject" data-block-id="${id}">Reject</button>
                    </div>
                </div>
                <pre><code class="language-${lang}">${displayCode}</code><div style="display:none" class="hidden-raw">${codeText}</div></pre>
            </div>`;
    }
    marked.setOptions({ renderer: renderer, gfm: true, breaks: true });

    // 🚀 1. STATE RESTORATION
    const state = vscode.getState() || { messages: [] };
    
    function saveState() {
        vscode.setState({ messages: Array.from(chatContainer.children).map(child => child.outerHTML) });
    }

    // Restore previous messages
    if (state.messages) {
        chatContainer.innerHTML = state.messages.join('');
        chatContainer.scrollTop = chatContainer.scrollHeight;
    }

    chatContainer.addEventListener('click', (e) => {
        const target = e.target;
        if (target.classList.contains('action-btn')) {
            const action = target.getAttribute('data-action');
            const blockId = target.getAttribute('data-block-id');
            const container = document.getElementById(blockId);
            
            if (action === 'accept') {
                const rawCode = container.querySelector('.hidden-raw').innerText;
                vscode.postMessage({ type: 'applyCode', value: rawCode, id: blockId });
            } else if (action === 'reject') {
                container.style.opacity = '0.4';
                container.querySelector('.code-actions').innerHTML = '<span>Rejected</span>';
                saveState(); // Save visual change
            }
        }
    });

    function handleSend() {
        const text = promptInput.value.trim();  
        if (!text) return;
        
        const div = document.createElement('div');
        div.className = 'message user';
        div.innerText = text;
        chatContainer.appendChild(div);
        
        vscode.postMessage({ type: 'askCode', value: text });
        promptInput.value = '';
        chatContainer.scrollTop = chatContainer.scrollHeight;
        saveState(); // Save new message
    }

    sendBtn.addEventListener('click', handleSend);
    promptInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' && !e.shiftKey) {
            e.preventDefault();
            handleSend();
        }
    });

    window.addEventListener('message', event => {
        const message = event.data;
        const bots = chatContainer.querySelectorAll('.message.bot');
        const lastBot = bots[bots.length - 1];

        switch (message.type) {
            case 'addResponse':
                const div = document.createElement('div');
                div.className = 'message bot';
                div.innerHTML = message.value; 
                chatContainer.appendChild(div);
                saveState();
                break;
            case 'updateLastResponse':
                if (lastBot) {
                    let content = message.value;
                    if (content.includes("[TARGET:") && !content.includes("```")) {
                        content = "```typescript\n" + content + "\n```";
                    }
                    lastBot.innerHTML = marked.parse(content);
                    saveState();
                }
                break;
            case 'applySuccess':
                const block = document.getElementById(message.id);
                if (block) {
                    block.style.borderColor = "#28a745";
                    block.querySelector('.code-actions').innerHTML = '<span style="color:#28a745; font-size:10px;">✓ Applied</span>';
                    saveState();
                }
                break;
        }
        chatContainer.scrollTop = chatContainer.scrollHeight;
    });
}());