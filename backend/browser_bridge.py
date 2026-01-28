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
                    viewport={'width': 1100, 'height': 500},
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

                # Grant clipboard permissions for the "Paste" strategy
                context.grant_permissions(["clipboard-read", "clipboard-write"], origin="https://aistudio.google.com")
                
                page = context.pages[0]
                
                # --- OPTIMIZATION: Block heavy assets ---
                def block_heavy_resources(route):
                    if route.request.resource_type in ["image", "font", "media"]:
                        route.abort()
                    else:
                        # FIXED: Added underscore because 'continue' is a Python keyword
                        route.continue_()
                
                # Turn on blocking (makes page lightweight)
                page.route("**/*", block_heavy_resources)
                
                # Stealth fix
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
        """Uploads a file and asks for extraction."""
        print(f"   [Thread] Starting File Extraction: {file_path}")
        
        try:
            if "aistudio.google.com" not in page.url:
                 page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded")

            if not os.path.exists(file_path):
                return "Error: File not found on server."

            filename = os.path.basename(file_path)
            
            # Open Add Media Menu
            add_btn = page.locator("[data-test-id='add-media-button']")
            add_btn.wait_for(state="visible", timeout=20000)
            add_btn.click()
            
            # Select Upload option
            upload_option = page.locator("button.mat-mdc-menu-item").filter(has_text="Upload a file")
            
            with page.expect_file_chooser() as fc_info:
                if upload_option.is_visible():
                    upload_option.click()
                else:
                    page.keyboard.press("Escape")
                    raise Exception("Upload menu option not found")
            
            fc_info.value.set_files(file_path)

            print(f"   [Thread] File '{filename}' selected. Waiting for attachment...")

            # Wait for file chip
            page.get_by_text(filename).wait_for(state="visible", timeout=40000)

            # Wait for processing bar to vanish (Tokenizing)
            # Use a short timeout check to see if it even appears
            try:
                page.locator("mat-progress-bar").wait_for(state="visible", timeout=2000)
                page.locator("mat-progress-bar").wait_for(state="hidden", timeout=120000)
            except:
                pass # Bar might have been too fast

            print("   [Thread] File attached. Sending prompt...")
            
            return self._internal_send_prompt(page, prompt, use_clipboard=False, skip_nav=True)

        except Exception as e:
            page.keyboard.press("Escape")
            return f"Upload/Extract Failed: {str(e)}"

    def _internal_send_prompt(self, page, message, use_clipboard=False, skip_nav=False):
        """Optimized prompt sender using Clipboard and UI State detection."""
        try:
            if not skip_nav:
                if "aistudio.google.com" not in page.url:
                     page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded")

            # 1. Locate Input Box
            prompt_box = page.get_by_placeholder("Start typing a prompt")
            prompt_box.wait_for(state="visible", timeout=30000)
            
            # 2. OPTIMIZATION: Use Clipboard Injection (Instant Paste)
            # Focus the box first
            prompt_box.click() 
            
            # Put text in clipboard programmatically
            # We escape backslashes for JS safety
            safe_msg = message.replace("\\", "\\\\").replace("`", "\\`").replace("$", "\\$")
            page.evaluate(f"navigator.clipboard.writeText(`{safe_msg}`)")
            
            # Perform Paste
            page.keyboard.press("Control+V")
            
            # 3. Click Run
            run_btn = page.locator('ms-run-button button').filter(has_text=re.compile(r"Run", re.IGNORECASE))
            
            run_btn.wait_for(state="visible", timeout=15000)
            
            # If the button is disabled, wait a moment for the paste to register
            if run_btn.is_disabled():
                time.sleep(0.6)
            
            run_btn.click()
            print("   [Thread] Run clicked. Waiting for generation...", end="", flush=True)

            # 4. OPTIMIZATION: State-Based Waiting (No loops)
            # We wait for the UI to enter "Generating" state (Stop button visible)
            # OR for it to finish immediately if it's super fast.
            
            stop_btn = page.locator("ms-run-button button").filter(has_text="Stop")
            
            try:
                # Wait for the Stop button to appear, indicating generation started
                stop_btn.wait_for(state="visible", timeout=5000)
                print(" Started...", end="", flush=True)
                
                # Now wait for Stop button to DISAPPEAR, indicating generation finished
                stop_btn.wait_for(state="hidden", timeout=120000)
                print(" Done.")
            except:
                # If we missed the "Stop" button appearing (too fast) or it failed
                # We check if Run is ready again
                print(" (Fast/Skip) ", end="", flush=True)

            # Double check Run button is back to ready state
            run_btn.wait_for(state="visible", timeout=10000)

            # 5. Capture Result
            # Grab the last text chunk immediately
            final_chunks = page.locator('ms-text-chunk').all()
            if not final_chunks: return "Error: No response chunks found."
            
            # Use inner_text for cleaner structure
            raw_answer = final_chunks[-1].inner_text()

            # 6. Clean Output
            clean_answer = self._clean_response(raw_answer)
            return clean_answer

        except Exception as e:
            return f"Browser Error: {str(e)}"

    def _clean_response(self, text):
        """Fast regex-based cleanup."""
        # Remove "Expand to view model thoughts"
        if "Expand to view model thoughts" in text:
            text = text.split("Expand to view model thoughts")[-1]

        # Regex to remove UI artifacts that stand alone
        ui_keywords = [
            "expand_more", "expand_less", "content_copy", "share", 
            "edit", "thumb_up", "thumb_down", "code", "json", "copy code"
        ]
        
        lines = text.split('\n')
        clean_lines = []
        
        for line in lines:
            stripped = line.strip()
            # Remove exact matches of UI words
            if stripped.lower() in ui_keywords:
                continue
            clean_lines.append(line)
            
        return '\n'.join(clean_lines).strip()

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
            selector = "ms-model-selector button span.title"
            return page.locator(selector).first.inner_text().strip()
        except:
            return "Unknown"

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

    # --- Public API methods (Unchanged interfaces) ---
    def send_prompt(self, message, use_clipboard=False):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("prompt", (message, use_clipboard), result_queue))
        try:
            return result_queue.get(timeout=250)
        except queue.Empty:
            return "Error: Browser bridge timed out."
    
    def extract_text_from_file(self, file_path):
        self.start()
        result_queue = queue.Queue()
        prompt = "Extract all text content from the attached file verbatim. Do not summarize. Just output the raw text."
        self.cmd_queue.put(("upload_extract", (file_path, prompt), result_queue))
        try:
            return result_queue.get(timeout=300) 
        except queue.Empty:
            return "Error: Browser bridge timed out during extraction."

    def get_bridge_state(self):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("get_state", None, result_queue))
        try:
            return result_queue.get(timeout=10)
        except queue.Empty:
            return {"models": [], "active": None}

    def reset(self):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("reset", None, result_queue))
        result_queue.get()

browser_bridge = AIStudioBridge()