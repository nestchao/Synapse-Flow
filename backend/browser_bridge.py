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
                
                page = context.pages[0]
                page.add_init_script("Object.defineProperty(navigator, 'webdriver', {get: () => undefined})")
                
                print("✅ [Thread] Browser Ready.")

                while True:
                    task = self.cmd_queue.get()
                    if task is None: break 
                    
                    cmd_type, data, result_queue = task
                    try:
                        if cmd_type == "prompt":
                            # Normal prompt, allow navigation/reset if needed
                            response = self._internal_send_prompt(page, data, skip_nav=False)
                            result_queue.put(response)
                        
                        elif cmd_type == "upload_extract":
                            # data is tuple: (file_path, prompt)
                            file_path, prompt = data
                            response = self._internal_upload_and_extract(page, file_path, prompt)
                            result_queue.put(response)

                        elif cmd_type == "reset":
                            page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="networkidle")
                            result_queue.put(True)

                        elif cmd_type == "get_state":
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
        
        # 1. Reset Chat first to ensure clean state
        try:
            # We want to start fresh so we don't attach to an old conversation
            if "aistudio.google.com" not in page.url:
                 page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="networkidle", timeout=60000)
            else:
                 # Check if the "New chat" button is visible and click it, otherwise reload
                 # This is safer than just reloading if the URL is generic
                 page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="networkidle")
        except:
            pass

        # 2. Upload Logic
        if not os.path.exists(file_path):
            return "Error: File not found on server."

        try:
            filename = os.path.basename(file_path)
            
            # Open Add Media Menu
            add_btn = page.locator("[data-test-id='add-media-button']")
            add_btn.wait_for(state="visible", timeout=20000)
            add_btn.click()
            time.sleep(0.5)
            
            # Handle File Chooser
            upload_option = page.locator("button.mat-mdc-menu-item").filter(has_text="Upload a file")
            
            with page.expect_file_chooser() as fc_info:
                if upload_option.is_visible():
                    upload_option.click()
                else:
                    # Fallback logic
                    page.keyboard.press("Escape")
                    raise Exception("Upload menu option not found")
            
            fc_info.value.set_files(file_path)

            print(f"   [Thread] File '{filename}' selected. Waiting for attachment...")

            # 3. Wait for file chip to appear
            try:
                page.get_by_text(filename).wait_for(state="visible", timeout=40000)
            except:
                print("   [Thread] Warning: Filename chip not detected within timeout. Proceeding anyway...")

            # 4. Wait for processing bar (Tokenizing)
            time.sleep(1) 
            try:
                if page.locator("mat-progress-bar").is_visible():
                    print("   [Thread] Processing bar detected. Waiting...")
                    page.locator("mat-progress-bar").wait_for(state="hidden", timeout=120000)
            except:
                pass

            print("   [Thread] File attached. Sending prompt...")
            
            # 5. Send Prompt with SKIP NAV enabled so we don't refresh the page
            return self._internal_send_prompt(page, prompt, skip_nav=True)

        except Exception as e:
            page.keyboard.press("Escape")
            return f"Upload/Extract Failed: {str(e)}"

    def _internal_send_prompt(self, page, message, skip_nav=False):
        """Logic executed strictly inside the worker thread."""
        try:
            # 1. Navigation
            if not skip_nav:
                try:
                    if page.is_closed() or "aistudio.google.com" not in page.url:
                        page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="networkidle", timeout=60000)
                except:
                    page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="networkidle", timeout=60000)

            # 2. Input
            # Wake up UI
            try:
                page.get_by_text("Start typing a prompt").click(timeout=3000)
            except:
                page.locator("textarea").last.click()
            
            time.sleep(0.5)

            page.evaluate("""
                (text) => {
                    const els = document.querySelectorAll('textarea');
                    const el = els[els.length - 1]; 
                    if (el) {
                        el.value = text;
                        el.innerText = text;
                        el.dispatchEvent(new Event('input', { bubbles: true }));
                    }
                }
            """, message)
            
            page.keyboard.press("Space")
            page.keyboard.press("Backspace")
            time.sleep(0.5)

            # 3. Send
            run_btn = page.locator('button[aria-label="Run"], button[aria-label="Send message"], mat-icon:has-text("send")').last
            if run_btn.is_visible() and not run_btn.is_disabled():
                 run_btn.click(force=True)
            else:
                 page.keyboard.press("Enter")

            print("   [Thread] Waiting for response...", end="", flush=True)

            # 4. ROBUST LOOP: Wait for NON-PROMPT response
            start_time = time.time()
            last_valid_len = 0
            stability_counter = 0
            
            while True:
                if time.time() - start_time > 400: return "Error: Timeout."

                try:
                    page.evaluate("window.scrollTo(0, document.body.scrollHeight)")
                except: pass

                # Get all text chunks
                # We use a broad selector to catch everything
                chunks = page.locator('ms-text-chunk, .model-thoughts, .chat-bubble, .message-content').all()
                
                if not chunks:
                    time.sleep(1)
                    continue

                # Grab the LAST chunk (most likely the new one)
                last_chunk_text = chunks[-1].inner_text().strip()
                
                # 🚀 CRITICAL CHECK: Are we looking at the System Prompt?
                # The user prompt contains "### SYSTEM ROLE" or "You are 'Synapse'"
                # The AI response SHOULD NOT contain this (unless it's hallucinating badly)
                is_user_prompt = "### SYSTEM ROLE" in last_chunk_text or "You are 'Synapse'" in last_chunk_text
                
                if is_user_prompt:
                    # We are seeing our own message. Wait for AI.
                    print("p", end="", flush=True) # 'p' for prompt
                    time.sleep(1.0)
                    continue

                # If we are here, 'last_chunk_text' is likely the AI response
                current_len = len(last_chunk_text)

                if current_len > 0:
                    if current_len == last_valid_len:
                        stability_counter += 1
                        print(".", end="", flush=True)
                        
                        # Stop button check
                        stop_visible = page.locator('button[aria-label="Stop"]').first.is_visible()
                        
                        if stability_counter >= 4 and not stop_visible:
                            print(" Captured.")
                            
                            # 5. Cleanup & Return
                            clean_answer = last_chunk_text.replace("Expand to view model thoughts", "").replace("Model", "").strip()
                            return clean_answer
                    else:
                        stability_counter = 0
                        print("*", end="", flush=True)
                    
                    last_valid_len = current_len
                else:
                    print("_", end="", flush=True)
                    stability_counter = 0
                
                time.sleep(1.0)

        except Exception as e:
            return f"Browser Error: {str(e)}"

    def _internal_get_models(self, page):
        """Scrapes available Gemini models from the UI."""
        print("   [Thread] Fetching models...")
        if "aistudio.google.com" not in page.url:
             page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="networkidle", timeout=60000)

        try:
            page.locator("ms-model-selector button").wait_for(state="visible", timeout=30000)
            model_btn = page.locator("ms-model-selector button")
            if not model_btn.is_visible():
                page.get_by_label("Run settings").click()
                time.sleep(0.5)
            model_btn.click()
            time.sleep(1.0)
            try:
                gemini_filter = page.locator("button.ms-button-filter-chip").filter(has_text="Gemini").first
                if gemini_filter.is_visible(): gemini_filter.click()
                time.sleep(0.5)
            except: pass 

            page.locator(".model-title-text").first.wait_for(timeout=3000)
            elements = page.locator(".model-title-text").all()
            models = list(dict.fromkeys([t.inner_text().strip() for t in elements if t.inner_text().strip()]))
            page.keyboard.press("Escape")
            return models
        except Exception as e:
            page.keyboard.press("Escape")
            return []

    def _internal_set_model(self, page, model_name):
        """Selects a specific model."""
        print(f"   [Thread] Switching to model: {model_name}...")
        if "aistudio.google.com" not in page.url:
             page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="networkidle", timeout=60000)

        try:
            page.locator("ms-model-selector button").wait_for(state="visible", timeout=30000)
            model_btn = page.locator("ms-model-selector button")
            if not model_btn.is_visible():
                page.get_by_label("Run settings").click()
                time.sleep(0.5)
            model_btn.click()
            time.sleep(1.0)
            try:
                gemini_filter = page.locator("button.ms-button-filter-chip").filter(has_text="Gemini").first
                if gemini_filter.is_visible(): gemini_filter.click()
                time.sleep(0.5)
            except: pass

            target = page.locator(".model-title-text").get_by_text(model_name, exact=True).first
            target.click()
            time.sleep(1.0)
            return True
        except Exception as e:
            page.keyboard.press("Escape")
            return f"Error: {e}"

    def send_prompt(self, message):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("prompt", message, result_queue))
        try:
            return result_queue.get(timeout=250)
        except queue.Empty:
            return "Error: Browser bridge timed out."
    
    def extract_text_from_file(self, file_path):
        """Uploads a file and extracts text using the browser."""
        self.start()
        result_queue = queue.Queue()
        prompt = "Extract all text content from the attached file verbatim. Do not summarize. Do not add markdown unless it is in the source. Just output the raw text."
        
        self.cmd_queue.put(("upload_extract", (file_path, prompt), result_queue))
        try:
            return result_queue.get(timeout=300) 
        except queue.Empty:
            return "Error: Browser bridge timed out during extraction."
    
    def _internal_get_active_model_name(self, page):
        """Scrapes the name of the model currently selected in the dropdown."""
        try:
            # Look for the model selector button text
            selector = page.locator("ms-model-selector button .mat-mdc-button-touch-target").first
            # If that fails, look for the title text in the settings side panel
            model_text_el = page.locator("ms-model-selector .mdc-button__label").first
            
            if model_text_el.is_visible():
                return model_text_el.inner_text().strip()
            return None
        except:
            return None

    def get_available_models(self):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("get_models", None, result_queue))
        try:
            return result_queue.get(timeout=60)
        except queue.Empty:
            return ["Error fetching"]

    def set_model(self, model_name):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("set_model", model_name, result_queue))
        try:
            return result_queue.get(timeout=60)
        except queue.Empty:
            return "Timeout"

    def get_bridge_state(self):
        """Returns the list of models AND the currently active one."""
        self.start()
        result_queue = queue.Queue()
        # We'll create a new task type for this
        self.cmd_queue.put(("get_state", None, result_queue))
        try:
            return result_queue.get(timeout=60)
        except queue.Empty:
            return {"models": [], "active": None}

    def reset(self):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("reset", None, result_queue))
        result_queue.get()

browser_bridge = AIStudioBridge()