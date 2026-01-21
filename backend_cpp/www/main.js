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
// Default to your test ID, but will update dynamically
let activeProjectId = "RDovUHJvamVjdHMvU0FfRVRG"; 

// --- NAVIGATION ---
window.switchPage = (pageName) => {
    UI.navItems.forEach(el => el.classList.remove('active'));
    // Handle the specific click case
    const btn = document.querySelector(`.nav-item[onclick*="${pageName}"]`);
    if(btn) btn.classList.add('active');

    UI.pages.forEach(el => el.classList.remove('active'));
    const page = document.getElementById(`view-${pageName}`);
    if(page) page.classList.add('active');
    
    if(pageName === 'graph') refreshGraph();
}

// --- GRAPH VISUALIZER ---
async function refreshGraph() {
    try {
        // Fetch graph for the active project
        const res = await fetch(`/api/admin/graph/${activeProjectId}`);
        const nodesData = await res.json();
        
        if (!nodesData || nodesData.length === 0) {
            UI.graphContainer.innerHTML = '<div style="color:#666; text-align:center; padding-top:50px">No graph data found for this project.</div>';
            return;
        }

        const nodes = new vis.DataSet();
        const edges = new vis.DataSet();
        
        nodesData.forEach(n => {
            let color = '#97c2fc'; 
            let shape = 'box';
            let label = n.type;

            // 🎨 Color Coding
            if (n.type === 'PROMPT') { color = '#bc8cff'; shape = 'ellipse'; label = 'USER'; }
            if (n.type === 'SYSTEM_THOUGHT') { color = '#58a6ff'; label = 'THINK'; }
            if (n.type === 'TOOL_CALL') { color = '#d29922'; label = 'TOOL'; }
            if (n.type === 'RESPONSE') { color = '#2ea043'; label = 'REPLY'; }
            if (n.type === 'CONTEXT_CODE') { color = '#30363d'; label = 'DATA'; }

            if (n.metadata && n.metadata.status === 'failed') color = '#ff7b72';

            nodes.add({ 
                id: n.id, 
                label: label, 
                title: formatCode(n.content), 
                color: color,
                shape: shape,
                font: { color: '#ffffff' }
            });

            if (n.parent_id) {
                edges.add({ from: n.parent_id, to: n.id, arrows: 'to', color: {color:'#555'} });
            }
        });

        const data = { nodes: nodes, edges: edges };
        const options = {
            layout: {
                hierarchical: {
                    direction: "UD",
                    sortMethod: "directed",
                    nodeSpacing: 180,
                    levelSeparation: 120
                }
            },
            physics: false,
            interaction: { hover: true }
        };
        
        if(network) network.destroy();
        network = new vis.Network(UI.graphContainer, data, options);
        
    } catch(e) { console.error("Graph Load Failed", e); }
}

// --- DATA POLLING ---
async function pollTelemetry() {
    try {
        const res = await fetch('/api/admin/telemetry');
        const data = await res.json();
        
        if(data.metrics) {
            UI.kpiCpu.innerText = (data.metrics.cpu || 0).toFixed(1) + '%';
            UI.kpiRam.innerText = (data.metrics.ram_mb || 0).toFixed(0) + 'MB';
            UI.kpiSync.innerText = (data.metrics.last_sync_duration_ms || 0).toFixed(0) + 'ms';
            UI.kpiCache.innerText = (data.metrics.cache_size_mb || 0).toFixed(2) + 'MB';
            UI.kpiLatency.innerText = (data.metrics.llm_latency || 0).toFixed(0) + 'ms';
            UI.kpiTps.innerText = (data.metrics.tps || 0).toFixed(1);
        }

        renderLogs(data.logs || []);
        updateAgentTrace(data.agent_traces || []);

        // Dynamic Project ID update: Grab ID from most recent log
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

function renderLogs(logs) {
    if (logs.length === lastLogCount) return;
    lastLogCount = logs.length;
    currentLogs = logs;

    if (logs.length === 0) {
        UI.logList.innerHTML = `<div style="padding:20px; color:#8b949e; text-align:center; font-size:12px">Waiting for transmission...</div>`;
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
                    <span class="type-tag ${typeClass}">${reqType}</span>
                    <span style="color:#666">${time}</span>
                </div>
                <div style="font-size:12px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; color: #8b949e;">
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
        container.innerHTML = '<div style="padding:40px; text-align:center; color:#555">Waiting for Agentic events...</div>';
        return;
    }

    container.innerHTML = traces.slice().reverse().map(t => {
        let colorClass = 'status-live';
        let icon = 'fa-circle';
        
        if (t.state === 'ERROR_CATCH') { colorClass = 'status-error'; icon = 'fa-exclamation-triangle'; }
        else if (t.state === 'TOOL_EXEC') { colorClass = 'status-tool'; icon = 'fa-cog'; }
        else if (t.state === 'THINKING') { colorClass = 'status-think'; icon = 'fa-brain'; }
        else if (t.state === 'FINAL') { colorClass = 'status-success'; icon = 'fa-flag-checkered'; }

        return `
            <div class="trace-item">
                <div class="trace-phase ${colorClass}">
                    <i class="fas ${icon}"></i> ${t.state}
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

    if (element) {
        document.querySelectorAll('.log-item').forEach(el => el.classList.remove('active'));
        element.classList.add('active');
    }

    UI.inspId.innerText = log.project_id || "UNKNOWN";
    const reqType = log.request_type || log.type || 'AGENT';
    UI.inspType.className = `badge type-${reqType}`;
    UI.inspType.innerText = reqType;
    
    UI.inspTime.innerText = new Date(log.timestamp * 1000).toLocaleTimeString();
    UI.inspDuration.innerText = (log.duration_ms || 0).toFixed(0) + "ms";
    UI.inspContextSize.innerText = (log.full_prompt ? log.full_prompt.length : 0).toLocaleString() + " chars";

    UI.inspUserInput.innerText = log.user_query;
    
    // 🚀 NEW: Apply color formatting to the prompt
    // We use a custom format function here that handles the specific markers
    let formattedPrompt = escapeHtml(log.full_prompt || "(No context captured)");
    
    // Apply syntax highlighting regex
    formattedPrompt = formattedPrompt
        .replace(/(&#35;&#35;&#35; SYSTEM ROLE)/g, '<span class="hl-system">$1</span>')
        .replace(/(&#35;&#35;&#35; TOOL MANIFEST)/g, '<span class="hl-tools">$1</span>')
        .replace(/(&#35;&#35;&#35; USER REQUEST)/g, '<span class="hl-user">$1</span>')
        .replace(/(&#35;&#35;&#35; 🛑 MANDATORY BUSINESS RULES)/g, '<span class="hl-alert">$1</span>')
        .replace(/(&#35;&#35;&#35; 📋 CURRENT EXECUTION PLAN)/g, '<span class="hl-plan">$1</span>')
        .replace(/(&#35;&#35;&#35; EXECUTION HISTORY)/g, '<span class="hl-history">$1</span>')
        .replace(/(&#35;&#35;&#35; ⚠️ PREVIOUS ERROR)/g, '<span class="hl-error">$1</span>');

    UI.inspFullPrompt.innerHTML = formattedPrompt;
    
    // Format response code blocks
    UI.inspResponse.innerHTML = formatCode(log.ai_response || "");

    renderVector(log.vector_snapshot);
};

function renderVector(vec) {
    if (vec && vec.length > 0) {
        UI.inspVector.innerHTML = vec.map(val => {
            const normalized = Math.max(-1, Math.min(1, val));
            let color = normalized > 0 
                ? `rgba(88, 166, 255, ${normalized})` 
                : `rgba(255, 123, 114, ${Math.abs(normalized)})`;
            return `<div class="vec-cell" style="background:${color}" title="${val.toFixed(4)}"></div>`;
        }).join('');
        UI.inspVector.innerHTML += `<div class="vec-val" style="margin-left:10px">${vec.length} dims</div>`;
    } else {
        UI.inspVector.innerHTML = '<span style="color:#555">No vector data available</span>';
    }
}

function formatCode(text) {
    if (!text) return "";
    let safe = escapeHtml(text);
    // Basic syntax highlighting for the response view
    safe = safe.replace(/\b(class|struct|if|else|return|void|int|string|const|auto|def|import|from)\b/g, '<span class="hl-k">$1</span>');
    safe = safe.replace(/(\/\/[^\n]*|#[^\n]*)/g, '<span class="hl-c">$1</span>'); // C++ and Python comments
    return safe;
}

function escapeHtml(text) {
    if (!text) return "";
    return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// 🚀 Start the Loop
setInterval(pollTelemetry, 1000);
pollTelemetry();