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
};

let currentLogs = [];
let lastLogCount = -1; // Start at -1 to force initial render

// --- NAVIGATION ---
window.switchPage = (pageName) => {
    UI.navItems.forEach(el => el.classList.remove('active'));
    document.querySelector(`.nav-item[onclick*="${pageName}"]`).classList.add('active');

    UI.pages.forEach(el => el.classList.remove('active'));
    document.getElementById(`view-${pageName}`).classList.add('active');
}

// --- DATA POLLING ---
async function pollTelemetry() {
    try {
        const res = await fetch('/api/admin/telemetry');
        const data = await res.json();
        
        // Debugging: Uncomment if still having issues
        // console.log("Backend Data:", data);

        // 1. Update KPIs (Add safety checks)
        if(data.metrics) {
            UI.kpiCpu.innerText = (data.metrics.cpu || 0).toFixed(1) + '%';
            UI.kpiRam.innerText = (data.metrics.ram_mb || 0).toFixed(0) + 'MB';
            UI.kpiSync.innerText = (data.metrics.last_sync_duration_ms || 0).toFixed(0) + 'ms';
            UI.kpiCache.innerText = (data.metrics.cache_size_mb || 0).toFixed(2) + 'MB';
            UI.kpiLatency.innerText = (data.metrics.llm_latency || 0).toFixed(0) + 'ms';
            UI.kpiTps.innerText = (data.metrics.tps || 0).toFixed(1);
        }

        // 2. Render Logs
        renderLogs(data.logs || []);

    } catch(e) { 
        console.error("Telemetry Poll Error (Check Console for details)", e); 
        UI.logList.innerHTML = `<div style="padding:20px; color:#ff7b72; text-align:center">Connection Lost</div>`;
    }
}

function renderLogs(logs) {
    if (logs.length === lastLogCount) return;
    lastLogCount = logs.length;
    currentLogs = logs;

    if (logs.length === 0) {
        UI.logList.innerHTML = `<div style="padding:20px; color:#8b949e; text-align:center; font-size:12px">Waiting for transmission...<br>(Use the Extension to generate data)</div>`;
        return;
    }

    UI.logList.innerHTML = logs.slice().reverse().map((log, index) => {
        const originalIndex = logs.length - 1 - index;
        const time = new Date(log.timestamp * 1000).toLocaleTimeString();
        
        // Handle undefined types gracefully
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

window.inspect = (index, element) => {
    const log = currentLogs[index];
    if(!log) return;

    document.querySelectorAll('.log-item').forEach(el => el.classList.remove('active'));
    if(element) element.classList.add('active');

    // Fill Inspector
    UI.inspId.innerText = log.project_id || "UNKNOWN";
    
    const reqType = log.request_type || log.type || 'AGENT';
    UI.inspType.className = `badge type-${reqType}`;
    UI.inspType.innerText = reqType;
    
    UI.inspTime.innerText = new Date(log.timestamp * 1000).toLocaleTimeString();
    UI.inspDuration.innerText = (log.duration_ms || 0).toFixed(0) + "ms";
    UI.inspContextSize.innerText = (log.full_prompt ? log.full_prompt.length : 0).toLocaleString() + " chars";

    UI.inspUserInput.innerText = log.user_query;
    UI.inspFullPrompt.innerHTML = formatCode(log.full_prompt || "(No context captured)");
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
    safe = safe.replace(/\b(class|struct|if|else|return|void|int|string|const|auto)\b/g, '<span class="hl-k">$1</span>');
    safe = safe.replace(/(\/\/[^\n]*)/g, '<span class="hl-c">$1</span>');
    safe = safe.replace(/(###.*)/g, '<span class="hl-m">$1</span>');
    return safe;
}

function escapeHtml(text) {
    if (!text) return "";
    return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

setInterval(pollTelemetry, 1000);
pollTelemetry();