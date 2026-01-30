# backend/browser_bridge.py
import os
import time
import threading
import queue
import re
from playwright.sync_api import sync_playwright

class AIStudioBridge:
    def __init__(self):
        self.cmd_queue = queue.Queue()
        self.worker_thread = None
        self.lock = threading.Lock()
        self.bot_profile_path = os.path.join(os.getcwd(), "chrome_stealth_profile")

    def start(self):
        with self.lock:
            if self.worker_thread and self.worker_thread.is_alive():
                return
            print("🚀 Starting Dedicated Chrome Bridge Thread...")
            self.worker_thread = threading.Thread(target=self._browser_loop, daemon=True)
            self.worker_thread.start()

    def _browser_loop(self):
        try:
            with sync_playwright() as p:
                print("   [Thread] Launching Optimized Chrome...")
                
                context = p.chromium.launch_persistent_context(
                    user_data_dir=self.bot_profile_path,
                    executable_path=r"C:\Program Files\Google\Chrome\Application\chrome.exe",
                    channel="chrome",
                    headless=False,
                    
                    # --- RAM & CPU OPTIMIZATIONS ---
                    viewport={'width': 1100, 'height': 800},
                    ignore_default_args=["--enable-automation"],
                    args=[
                        "--start-maximized", 
                        "--disable-blink-features=AutomationControlled",
                        "--disable-gpu",
                        "--disable-dev-shm-usage",
                        "--no-sandbox",
                        "--js-flags='--max-old-space-size=512'"
                    ]
                )

                context.grant_permissions(["clipboard-read", "clipboard-write"], origin="https://aistudio.google.com")
                
                page = context.pages[0]
                
                # --- OPTIMIZATION: Block heavy assets ---
                def block_heavy_resources(route):
                    if route.request.resource_type in ["image", "font", "media"]:
                        route.abort()
                    else:
                        route.continue_()
                
                page.route("**/*", block_heavy_resources)
                page.add_init_script("Object.defineProperty(navigator, 'webdriver', {get: () => undefined})")
                
                print("✅ [Thread] Browser Ready.")

                while True:
                    task = self.cmd_queue.get()
                    if task is None: break 
                    
                    cmd_type, data, result_queue = task
                    try:
                        if cmd_type == "prompt":
                            msg, use_clip = data
                            response = self._internal_send_prompt(page, msg, use_clipboard=use_clip, skip_nav=False)
                            result_queue.put(response)
                        
                        elif cmd_type == "upload_extract":
                            file_path, prompt = data
                            response = self._internal_upload_and_extract(page, file_path, prompt)
                            result_queue.put(response)

                        elif cmd_type == "reset":
                            page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded")
                            result_queue.put(True)

                        elif cmd_type == "get_state":
                            if "aistudio.google.com/app" not in page.url:
                                page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded")
                            
                            models = self._internal_get_models(page)
                            active = self._internal_get_active_model_name(page)
                            result_queue.put({"models": models, "active": active})
                            
                        elif cmd_type == "get_models":
                            models = self._internal_get_models(page)
                            result_queue.put(models)
                        elif cmd_type == "set_model":
                            success = self._internal_set_model(page, data)
                            result_queue.put(success)
                    except Exception as e:
                        print(f"❌ [Thread] Error processing {cmd_type}: {e}")
                        result_queue.put(f"Bridge Error: {str(e)}")
                    finally:
                        self.cmd_queue.task_done()
        except Exception as e:
            print(f"❌ [Thread] CRITICAL BRIDGE FAILURE: {e}")

    def _internal_upload_and_extract(self, page, file_path, prompt):
        print(f"   [Thread] Starting File Extraction: {file_path}")
        try:
            if "aistudio.google.com" not in page.url:
                 page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded")

            if not os.path.exists(file_path): return "Error: File not found on server."
            filename = os.path.basename(file_path)
            
            add_btn = page.locator("[data-test-id='add-media-button']")
            add_btn.wait_for(state="visible", timeout=20000)
            add_btn.click()
            
            upload_option = page.locator("button.mat-mdc-menu-item").filter(has_text="Upload a file")
            with page.expect_file_chooser() as fc_info:
                if upload_option.is_visible():
                    upload_option.click()
                else:
                    page.keyboard.press("Escape")
                    raise Exception("Upload menu option not found")
            
            fc_info.value.set_files(file_path)
            print(f"   [Thread] File '{filename}' selected. Waiting for attachment...")
            page.get_by_text(filename).wait_for(state="visible", timeout=40000)
            
            try:
                page.locator("mat-progress-bar").wait_for(state="visible", timeout=2000)
                page.locator("mat-progress-bar").wait_for(state="hidden", timeout=120000)
            except: pass

            print("   [Thread] File attached. Sending prompt...")
            return self._internal_send_prompt(page, prompt, use_clipboard=False, skip_nav=True)

        except Exception as e:
            page.keyboard.press("Escape")
            return f"Upload/Extract Failed: {str(e)}"

    def _internal_send_prompt(self, page, message, use_clipboard=False, skip_nav=False):
        """FAST & ROBUST sending logic."""
        try:
            if not skip_nav:
                if "aistudio.google.com" not in page.url:
                     page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded")

            # 1. Fast Paste
            prompt_box = page.get_by_placeholder("Start typing a prompt")
            prompt_box.wait_for(state="visible", timeout=30000)
            prompt_box.click() 
            
            safe_msg = message.replace("\\", "\\\\").replace("`", "\\`").replace("$", "\\$")
            page.evaluate(f"navigator.clipboard.writeText(`{safe_msg}`)")
            page.keyboard.press("Control+V")
            time.sleep(0.3)
            
            # 2. Click Run
            run_btn = page.locator('button[aria-label="Run"]')
            if not run_btn.is_visible():
                run_btn = page.locator("button:has-text('Run')").last
            
            run_btn.wait_for(state="visible", timeout=15000)
            if run_btn.is_disabled(): time.sleep(0.6)
            
            run_btn.click()
            print("   [Thread] Run clicked. Waiting for generation...", end="", flush=True)

            # 3. State-Based Waiting (Fastest Possible)
            stop_btn = page.locator('button[aria-label="Stop generating"]')
            try:
                stop_btn.wait_for(state="visible", timeout=5000)
                print(" Started...", end="", flush=True)
                stop_btn.wait_for(state="hidden", timeout=120000)
                print(" Done.")
            except:
                print(" (Fast/Skip) ", end="", flush=True)

            run_btn.wait_for(state="visible", timeout=10000)
            time.sleep(0.5)

            print("   [Thread] Waiting for completion...", end="", flush=True)
            
            # Wait for "Stop" button to appear (Active Generation)
            try:
                page.locator('button[aria-label="Stop generating"]').wait_for(state="visible", timeout=5000)
            except:
                pass # Might have missed it if fast

            # Poll for text stability (The "Silence" Check)
            last_text_len = 0
            stable_count = 0
            
            for _ in range(60): # Max 30 seconds wait for stability
                try:
                    # Check if "Stop" button is gone
                    if not page.locator('button[aria-label="Stop generating"]').is_visible():
                        # Get text length
                        curr_text = page.locator('ms-chat-turn').last.inner_text()
                        if len(curr_text) == last_text_len and len(curr_text) > 10:
                            stable_count += 1
                        else:
                            stable_count = 0
                        
                        last_text_len = len(curr_text)
                        
                        # If text hasn't changed for 1.5 seconds and Stop button is gone, we are done
                        if stable_count >= 3:
                            print(" Done.")
                            break
                except:
                    pass
                
                time.sleep(0.5)

            # Extra buffer for rendering
            time.sleep(0.5)

            return self._extract_full_response(page)

        except Exception as e:
            return f"Browser Error: {str(e)}"

    def _extract_full_response(self, page):
        """Robustly extracts ALL text from the last turn."""
        try:
            last_turn = page.locator('ms-chat-turn').last
            if not last_turn.is_visible(): return "Error: No chat turn found."

            # Use JS to iterate ALL chunks in the turn
            full_text = last_turn.evaluate("""(turn) => {
                const chunks = turn.querySelectorAll('ms-text-chunk');
                let result = "";
                chunks.forEach(chunk => {
                    const isThought = chunk.closest('ms-model-thoughts');
                    if (!isThought) {
                        result += chunk.innerText + "\\n";
                    }
                });
                return result;
            }""")
            
            if not full_text.strip():
                full_text = last_turn.inner_text()

            return self._clean_response(full_text)

        except Exception as e:
            print(f"   [Thread] Extraction error: {e}")
            return f"Extraction Failed: {str(e)}"

    def _clean_response(self, text):
        if not text: return text
        if "Expand to view model thoughts" in text:
            text = text.split("Expand to view model thoughts")[-1]
        
        ui_artifacts = [
            "expand_more", "expand_less", "content_copy", "share", "volume_up",
            "edit", "thumb_up", "thumb_down", "more_vert", "download",
            "code", "json", "copy code", "editmore_vert", "delete"
        ]
        
        lines = text.split('\n')
        cleaned_lines = []
        for line in lines:
            stripped = line.strip().lower()
            if stripped in ui_artifacts: continue
            if len(stripped.split()) == 1 and any(art in stripped for art in ui_artifacts): continue
            cleaned_lines.append(line)
        
        text = '\n'.join(cleaned_lines)
        text = re.sub(r'\n\s*\n\s*\n+', '\n\n', text)
        text = re.sub(r'\n\s*download\s*\n', '\n', text, flags=re.IGNORECASE)
        text = re.sub(r'\n\s*copy\s*\n', '\n', text, flags=re.IGNORECASE)
        
        return text.strip()

    def _internal_get_models(self, page):
        if "aistudio.google.com/app" not in page.url:
             page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded")
        try:
            page.locator("ms-model-selector button").wait_for(state="visible", timeout=5000)
            page.locator("ms-model-selector button").click()
            page.locator(".model-title-text").first.wait_for(timeout=3000)
            elements = page.locator(".model-title-text").all()
            models = list(dict.fromkeys([t.inner_text().strip() for t in elements if t.inner_text().strip()]))
            page.keyboard.press("Escape")
            return models
        except Exception:
            page.keyboard.press("Escape")
            return []

    def _internal_get_active_model_name(self, page):
        try:
            return page.locator("ms-model-selector button span.title").first.inner_text().strip()
        except: return "Unknown"

    def _internal_set_model(self, page, model_name):
        try:
            page.locator("ms-model-selector button").click()
            page.locator(".model-title-text").first.wait_for(timeout=3000)
            target = page.locator(".model-title-text").get_by_text(model_name, exact=True).first
            if target.is_visible():
                target.click()
                return True
            else:
                page.keyboard.press("Escape")
                return False
        except:
            page.keyboard.press("Escape")
            return False

    def send_prompt(self, message, use_clipboard=False):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("prompt", (message, use_clipboard), result_queue))
        try: return result_queue.get(timeout=250)
        except queue.Empty: return "Error: Browser bridge timed out."
    
    def extract_text_from_file(self, file_path):
        self.start()
        result_queue = queue.Queue()
        prompt = "Extract all text content from the attached file verbatim. Do not summarize. Just output the raw text."
        self.cmd_queue.put(("upload_extract", (file_path, prompt), result_queue))
        try: return result_queue.get(timeout=300) 
        except queue.Empty: return "Error: Browser bridge timed out during extraction."

    def get_bridge_state(self):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("get_state", None, result_queue))
        try: return result_queue.get(timeout=10)
        except queue.Empty: return {"models": [], "active": None}

    def reset(self):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("reset", None, result_queue))
        result_queue.get()

browser_bridge = AIStudioBridge()