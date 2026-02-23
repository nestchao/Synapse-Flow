// extension/media/chat.js
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
        const messages = Array.from(chatContainer.children).slice(-50).map(child => child.outerHTML);
        vscode.setState({ messages });
    }

    if (state.messages && state.messages.length > 0) {
        chatContainer.innerHTML = state.messages.join('');
        setTimeout(() => chatContainer.scrollTop = chatContainer.scrollHeight, 100);
    }

    // --- GLOBAL FUNCTIONS (Must be accessible by HTML onclick) ---
    window.sendApproval = (id, approved) => {
        const card = document.getElementById(id);
        if(card) {
            // Visual feedback
            const btns = card.querySelector('.plan-actions');
            if(btns) btns.innerHTML = approved ? 
                '<span style="color:#20c997; font-weight:bold; width:100%; text-align:center;">MISSION AUTHORIZED</span>' : 
                '<span style="color:#dc143c; font-weight:bold; width:100%; text-align:center;">MISSION ABORTED</span>';
        }
        // Send to extension
        vscode.postMessage({ 
            type: 'askCode', 
            value: approved ? "Plan approved. Proceed with execution." : "Plan rejected. Stop." 
        });
    };

    // --- EVENT LISTENERS ---
    chatContainer.addEventListener('click', (e) => {
        const target = e.target;
        if (target.classList.contains('action-btn')) {
            const action = target.getAttribute('data-action');
            const blockId = target.getAttribute('data-block-id');
            const container = document.getElementById(blockId);
            
            if (action === 'accept') {
                const rawCode = container.querySelector('.hidden-raw').innerText;
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
        
        // 2. Add Thinking Placeholder (UPDATED FOR ANIMATION)
        const thinkingDiv = document.createElement('div');
        thinkingDiv.className = 'message bot thinking';
        thinkingDiv.id = 'temp-thinking';
        // 🚀 FIX: Use innerHTML to inject the CSS magic circle structure
        thinkingDiv.innerHTML = `
            <div class="thinking-container">
                <div class="magic-circle"></div>
                <span class="thinking-text">Weaving Logic...</span>
            </div>
        `;
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
        
        const tempThinking = document.getElementById('temp-thinking');
        if (tempThinking) tempThinking.remove();

        const bots = chatContainer.querySelectorAll('.message.bot');
        const lastBot = bots[bots.length - 1];

        switch (message.type) {
            case 'addResponse':
                const div = document.createElement('div');
                // Check if backend specifically sent "Thinking..." text update
                if (message.value.includes('Thinking')) {
                    div.className = 'message bot thinking';
                    div.innerHTML = `
                        <div class="thinking-container">
                            <div class="magic-circle"></div>
                            <span class="thinking-text">${message.value}</span>
                        </div>
                        <div id="live-trace-container" style="margin-top: 10px; font-size: 11px; color: #a5d6ff; border-left: 2px solid #9370db; padding-left: 10px; font-family: monospace;">
                            <div style="opacity: 0.6">Initializing neural link...</div>
                        </div>`;
                } else {
                    div.className = 'message bot';
                    div.innerHTML = marked.parse(message.value);
                }
                chatContainer.appendChild(div);
                saveState();
                break;
                
            case 'updateLastResponse':
                let content = message.value;
                if ((content.includes("def ") || content.includes("class ") || content.includes("import ")) && !content.includes("```")) {
                    content = "```python\n" + content + "\n```";
                }
                
                if (!lastBot) {
                    const newDiv = document.createElement('div');
                    newDiv.className = 'message bot';
                    newDiv.innerHTML = marked.parse(content);
                    chatContainer.appendChild(newDiv);
                } else {
                    lastBot.className = 'message bot'; // Removes 'thinking' class and animation
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

            case 'renderPlan':
                // 🚀 FIX: Logic correctly placed inside switch
                const plan = message.value; 
                const planId = 'plan-' + Math.random().toString(36).substr(2, 9);
                
                let stepsHtml = plan.steps.map((step, idx) => `
                    <div class="step-item step-${step.status.toLowerCase()}">
                        <div class="step-icon">${step.status === 'SUCCESS' ? '✓' : (idx + 1)}</div>
                        <div class="step-desc">
                            <span style="color:#ffd700; font-weight:bold">[${step.tool}]</span> 
                            ${step.description}
                        </div>
                    </div>
                `).join('');

                const html = `
                    <div class="plan-card" id="${planId}">
                        <div class="plan-header">
                            <span>⚡ EXECUTION PLAN</span>
                            <span style="font-size:10px; opacity:0.7">${plan.status}</span>
                        </div>
                        <div class="plan-steps">${stepsHtml}</div>
                        ${plan.status === 'REVIEW_REQUIRED' ? `
                        <div class="plan-actions">
                            <button class="btn-approve" onclick="sendApproval('${planId}', true)">APPROVE MISSION</button>
                            <button class="btn-reject" onclick="sendApproval('${planId}', false)">ABORT</button>
                        </div>` : ''}
                    </div>
                `;

                const planDiv = document.createElement('div');
                planDiv.className = 'message bot';
                planDiv.innerHTML = html;
                chatContainer.appendChild(planDiv);
                saveState();
                break;

            case 'traceUpdate':
                const traceContainer = document.getElementById('live-trace-container');
                if (traceContainer && message.traces) {
                    // Get the 3 most recent traces to keep the UI clean
                    const recentTraces = message.traces.slice(-3);
                    traceContainer.innerHTML = recentTraces.map(t => {
                        let icon = '⚡'; // default
                        if (t.state === 'TOOL_EXEC') icon = '⚙️';
                        if (t.state === 'ERROR_CATCH') icon = '❌';
                        if (t.state === 'FINAL') icon = '✅';
                        
                        // Clean up the text so it fits nicely
                        let safeText = escapeHtml(t.detail).replace(/\n/g, ' ');
                        if (safeText.length > 55) safeText = safeText.substring(0, 55) + '...';
                        
                        return `<div style="margin-bottom: 4px;">${icon} [${t.state}] ${safeText}</div>`;
                    }).join('');
                    
                    chatContainer.scrollTop = chatContainer.scrollHeight;
                }
                break;

        }
        chatContainer.scrollTop = chatContainer.scrollHeight;
    });
}());