const UI = {
    // Navigation
    navItems: document.querySelectorAll('.nav-item'),
    pages: document.querySelectorAll('.view-section'),

    // Logs
    logList: document.getElementById('log-list'),
    
    // KPIs
    kpiCpu: document.getElementById('kpi-cpu'),
    kpiRam: document.getElementById('kpi-ram'),
    kpiSync: document.getElementById('kpi-sync'),
    kpiCache: document.getElementById('kpi-cache'),
    kpiLatency: document.getElementById('kpi-latency'),
    kpiTps: document.getElementById('kpi-tps'),
    
    // Inspector
    inspId: document.getElementById('insp-id'),
    inspType: document.getElementById('insp-type'),
    inspTime: document.getElementById('insp-time'),
    inspDuration: document.getElementById('insp-duration'),
    inspContextSize: document.getElementById('insp-context-size'),
    inspFullPrompt: document.getElementById('insp-full-prompt'),
    inspResponse: document.getElementById('insp-response'),
    inspUserInput: document.getElementById('insp-user-input'),
    inspVector: document.getElementById('insp-vector'),
    
    // Graph
    graphContainer: document.getElementById('graph-container')
};

let currentLogs = [];
let lastLogCount = -1; 
let network = null;
let activeProjectId = "RDovUHJvamVjdHMvU0FfRVRG"; 

// 🎴 Touhou Enhancement: Particle burst on navigation
function createParticleBurst(element) {
    const rect = element.getBoundingClientRect();
    const colors = ['#ff69b4', '#9370db', '#ffd700'];
    
    for (let i = 0; i < 8; i++) {
        const particle = document.createElement('div');
        particle.style.position = 'fixed';
        particle.style.left = rect.left + rect.width / 2 + 'px';
        particle.style.top = rect.top + rect.height / 2 + 'px';
        particle.style.width = '4px';
        particle.style.height = '4px';
        particle.style.borderRadius = '50%';
        particle.style.background = colors[Math.floor(Math.random() * colors.length)];
        particle.style.pointerEvents = 'none';
        particle.style.zIndex = '9999';
        particle.style.boxShadow = `0 0 10px ${colors[Math.floor(Math.random() * colors.length)]}`;
        
        document.body.appendChild(particle);
        
        const angle = (Math.PI * 2 * i) / 8;
        const velocity = 50 + Math.random() * 30;
        const tx = Math.cos(angle) * velocity;
        const ty = Math.sin(angle) * velocity;
        
        particle.animate([
            { transform: 'translate(0, 0) scale(1)', opacity: 1 },
            { transform: `translate(${tx}px, ${ty}px) scale(0)`, opacity: 0 }
        ], {
            duration: 600,
            easing: 'cubic-bezier(0.4, 0.0, 0.6, 1)'
        }).onfinish = () => particle.remove();
    }
}

// --- NAVIGATION ---
window.switchPage = (pageName) => {
    const btn = document.querySelector(`.nav-item[onclick*="${pageName}"]`);
    
    // 🎴 Particle effect on switch
    if (btn) {
        createParticleBurst(btn);
    }
    
    UI.navItems.forEach(el => el.classList.remove('active'));
    if(btn) btn.classList.add('active');

    UI.pages.forEach(el => el.classList.remove('active'));
    const page = document.getElementById(`view-${pageName}`);
    if(page) page.classList.add('active');
    
    if(pageName === 'graph') refreshGraph();
}

// --- GRAPH VISUALIZER ---
async function refreshGraph() {
    try {
        const res = await fetch(`/api/admin/graph/${activeProjectId}`);
        const nodesData = await res.json();
        
        if (!nodesData || nodesData.length === 0) {
            UI.graphContainer.innerHTML = '<div style="color:#8b949e; text-align:center; padding-top:50px; font-family:monospace;">No graph data found for this project.</div>';
            return;
        }

        const nodes = new vis.DataSet();
        const edges = new vis.DataSet();
        
        nodesData.forEach(n => {
            let color = '#97c2fc'; 
            let shape = 'box';
            let label = n.type;

            // 🎨 Touhou Color Coding
            if (n.type === 'PROMPT') { 
                color = '#bc8cff'; 
                shape = 'ellipse'; 
                label = '✦ USER';
            }
            if (n.type === 'SYSTEM_THOUGHT') { 
                color = '#58a6ff'; 
                label = '⚡ THINK';
            }
            if (n.type === 'TOOL_CALL') { 
                color = '#d29922'; 
                label = '🔧 TOOL';
            }
            if (n.type === 'RESPONSE') { 
                color = '#2ea043'; 
                label = '✓ REPLY';
            }
            if (n.type === 'CONTEXT_CODE') { 
                color = '#30363d'; 
                label = '📦 DATA';
            }

            if (n.metadata && n.metadata.status === 'failed') {
                color = '#ff7b72';
                label = '✗ ' + label;
            }

            nodes.add({ 
                id: n.id, 
                label: label, 
                title: formatCode(n.content), 
                color: {
                    background: color,
                    border: '#fff',
                    highlight: {
                        background: color,
                        border: '#ffd700'
                    }
                },
                shape: shape,
                font: { 
                    color: '#ffffff',
                    size: 14,
                    face: 'Segoe UI'
                },
                borderWidth: 2,
                shadow: {
                    enabled: true,
                    color: color,
                    size: 10,
                    x: 0,
                    y: 0
                }
            });

            if (n.parent_id) {
                edges.add({ 
                    from: n.parent_id, 
                    to: n.id, 
                    arrows: {
                        to: {
                            enabled: true,
                            scaleFactor: 0.8
                        }
                    },
                    color: {
                        color: '#9370db',
                        highlight: '#ff69b4',
                        hover: '#ff69b4'
                    },
                    width: 2,
                    smooth: {
                        enabled: true,
                        type: 'cubicBezier',
                        roundness: 0.5
                    }
                });
            }
        });

        const data = { nodes: nodes, edges: edges };
        const options = {
            layout: {
                hierarchical: {
                    direction: "UD",
                    sortMethod: "directed",
                    nodeSpacing: 200,
                    levelSeparation: 150
                }
            },
            physics: {
                enabled: false
            },
            interaction: { 
                hover: true,
                tooltipDelay: 100,
                zoomView: true
            },
            nodes: {
                borderWidthSelected: 3
            }
        };
        
        if(network) network.destroy();
        network = new vis.Network(UI.graphContainer, data, options);
        
        // 🎴 Add click effect on nodes
        network.on("click", function(params) {
            if (params.nodes.length > 0) {
                const clickedElement = document.elementFromPoint(params.pointer.DOM.x, params.pointer.DOM.y);
                if (clickedElement) {
                    createParticleBurst(clickedElement);
                }
            }
        });
        
    } catch(e) { 
        console.error("Graph Load Failed", e); 
        UI.graphContainer.innerHTML = '<div style="color:#ff1744; text-align:center; padding-top:50px; font-family:monospace;">⚠️ Graph loading failed</div>';
    }
}

// --- DATA POLLING ---
async function pollTelemetry() {
    try {
        const res = await fetch('/api/admin/telemetry');
        const data = await res.json();
        
        if(data.metrics) {
            // 🎴 Animate value changes
            animateValue(UI.kpiCpu, (data.metrics.cpu || 0).toFixed(1) + '%');
            animateValue(UI.kpiRam, (data.metrics.ram_mb || 0).toFixed(0) + 'MB');
            animateValue(UI.kpiSync, (data.metrics.last_sync_duration_ms || 0).toFixed(0) + 'ms');
            animateValue(UI.kpiCache, (data.metrics.cache_size_mb || 0).toFixed(2) + 'MB');
            animateValue(UI.kpiLatency, (data.metrics.llm_latency || 0).toFixed(0) + 'ms');
            animateValue(UI.kpiTps, (data.metrics.tps || 0).toFixed(1));
        }

        renderLogs(data.logs || []);
        updateAgentTrace(data.agent_traces || []);

        // Dynamic Project ID update
        if (data.logs && data.logs.length > 0) {
            const lastLog = data.logs[data.logs.length - 1];
            if (lastLog.project_id && lastLog.project_id !== "default") {
                activeProjectId = lastLog.project_id;
            }
        }

    } catch(e) { 
        console.error("Telemetry Poll Error", e); 
    }
}

// 🎴 Smooth value animation
function animateValue(element, newValue) {
    if (element.textContent !== newValue) {
        element.style.transform = 'scale(1.15)';
        element.style.textShadow = '0 0 20px currentColor';
        setTimeout(() => {
            element.textContent = newValue;
            element.style.transform = 'scale(1)';
            element.style.textShadow = '0 0 10px rgba(255, 105, 180, 0.5)';
        }, 150);
    }
}

function renderLogs(logs) {
    if (logs.length === lastLogCount) return;
    lastLogCount = logs.length;
    currentLogs = logs;

    if (logs.length === 0) {
        UI.logList.innerHTML = `<div style="padding:20px; color:rgba(232,213,255,0.5); text-align:center; font-size:12px; font-style:italic;">⏳ Waiting for transmission...</div>`;
        return;
    }

    UI.logList.innerHTML = logs.slice().reverse().map((log, index) => {
        const originalIndex = logs.length - 1 - index;
        const time = new Date(log.timestamp * 1000).toLocaleTimeString();
        const reqType = log.request_type || log.type || 'UNKNOWN';
        const typeClass = reqType === 'GHOST' ? 'type-GHOST' : 'type-AGENT';
        
        return `
            <div class="log-item" onclick='inspect(${originalIndex}, this)'>
                <div class="log-top">
                    <span class="type-tag ${typeClass}">✦ ${reqType}</span>
                    <span style="color:rgba(232,213,255,0.6); font-family:monospace;">${time}</span>
                </div>
                <div style="font-size:12px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; color: rgba(232,213,255,0.8); padding-top:4px;">
                    ${escapeHtml(log.user_query || "No query data")}
                </div>
            </div>
        `;
    }).join('');
}

let lastTraceCount = 0;
function updateAgentTrace(traces) {
    if (traces.length === lastTraceCount) return;
    lastTraceCount = traces.length;

    const container = document.getElementById('trace-list');
    if (!container) return;

    if (traces.length === 0) {
        container.innerHTML = '<div style="padding:40px; text-align:center; color:rgba(232,213,255,0.4); font-style:italic;">⏳ Waiting for Agentic events...</div>';
        return;
    }

    container.innerHTML = traces.slice().reverse().map(t => {
        let colorClass = 'status-live';
        let icon = 'fa-circle';
        let emoji = '●';
        
        if (t.state === 'ERROR_CATCH') { 
            colorClass = 'status-error'; 
            icon = 'fa-exclamation-triangle';
            emoji = '✗';
        }
        else if (t.state === 'TOOL_EXEC') { 
            colorClass = 'status-tool'; 
            icon = 'fa-cog';
            emoji = '⚙';
        }
        else if (t.state === 'THINKING') { 
            colorClass = 'status-think'; 
            icon = 'fa-brain';
            emoji = '⚡';
        }
        else if (t.state === 'FINAL') { 
            colorClass = 'status-success'; 
            icon = 'fa-flag-checkered';
            emoji = '✓';
        }

        return `
            <div class="trace-item">
                <div class="trace-phase ${colorClass}">
                    <i class="fas ${icon}"></i> ${emoji} ${t.state}
                </div>
                <div class="trace-payload">
                    ${escapeHtml(t.detail)}
                </div>
                <div class="trace-time">${t.duration ? t.duration.toFixed(0) + 'ms' : ''}</div>
            </div>
        `;
    }).join('');
}

window.inspect = (index, element) => {
    const log = currentLogs[index];
    if(!log) return;

    // 🎴 Particle effect on inspection
    if (element) {
        document.querySelectorAll('.log-item').forEach(el => el.classList.remove('active'));
        element.classList.add('active');
        createParticleBurst(element);
    }

    UI.inspId.innerText = log.project_id || "UNKNOWN";
    const reqType = log.request_type || log.type || 'AGENT';
    UI.inspType.className = `badge type-${reqType}`;
    UI.inspType.innerText = '【 ' + reqType + ' 】';
    
    UI.inspTime.innerText = new Date(log.timestamp * 1000).toLocaleTimeString();
    UI.inspDuration.innerText = (log.duration_ms || 0).toFixed(0) + "ms";
    UI.inspContextSize.innerText = (log.full_prompt ? log.full_prompt.length : 0).toLocaleString() + " chars";

    UI.inspUserInput.innerText = log.user_query;
    
    // Apply color formatting to the prompt
    let formattedPrompt = escapeHtml(log.full_prompt || "(No context captured)");
    
    formattedPrompt = formattedPrompt
        .replace(/(&#35;&#35;&#35; SYSTEM ROLE)/g, '<span class="hl-system">$1</span>')
        .replace(/(&#35;&#35;&#35; TOOL MANIFEST)/g, '<span class="hl-tools">$1</span>')
        .replace(/(&#35;&#35;&#35; USER REQUEST)/g, '<span class="hl-user">$1</span>')
        .replace(/(&#35;&#35;&#35; 🛑 MANDATORY BUSINESS RULES)/g, '<span class="hl-alert">$1</span>')
        .replace(/(&#35;&#35;&#35; 📋 CURRENT EXECUTION PLAN)/g, '<span class="hl-plan">$1</span>')
        .replace(/(&#35;&#35;&#35; EXECUTION HISTORY)/g, '<span class="hl-history">$1</span>')
        .replace(/(&#35;&#35;&#35; ⚠️ PREVIOUS ERROR)/g, '<span class="hl-error">$1</span>');

    UI.inspFullPrompt.innerHTML = formattedPrompt;
    UI.inspResponse.innerHTML = formatCode(log.ai_response || "");

    renderVector(log.vector_snapshot);
};

function renderVector(vec) {
    if (vec && vec.length > 0) {
        UI.inspVector.innerHTML = vec.map(val => {
            const normalized = Math.max(-1, Math.min(1, val));
            let color = normalized > 0 
                ? `rgba(88, 166, 255, ${normalized})` 
                : `rgba(255, 23, 68, ${Math.abs(normalized)})`;
            return `<div class="vec-cell" style="background:${color}" title="${val.toFixed(4)}"></div>`;
        }).join('');
        UI.inspVector.innerHTML += `<div class="vec-val">✦ ${vec.length} dims</div>`;
    } else {
        UI.inspVector.innerHTML = '<span style="color:rgba(232,213,255,0.4); font-style:italic;">No vector data available</span>';
    }
}

function formatCode(text) {
    if (!text) return "";
    let safe = escapeHtml(text);
    safe = safe.replace(/\b(class|struct|if|else|return|void|int|string|const|auto|def|import|from)\b/g, '<span class="hl-k">$1</span>');
    safe = safe.replace(/(\/\/[^\n]*|#[^\n]*)/g, '<span class="hl-c">$1</span>');
    return safe;
}

function escapeHtml(text) {
    if (!text) return "";
    return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// 🎴 Start with a magical initialization
console.log('%c✦ Synapse-Flow Mission Control ✦', 'color: #ff69b4; font-size: 20px; font-weight: bold; text-shadow: 0 0 10px #ff69b4');
console.log('%cTouhou-Style Interface Active', 'color: #9370db; font-size: 14px;');

setInterval(pollTelemetry, 1000);
pollTelemetry();