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
                def block_aggressively(route):
                    if route.request.resource_type in ["image", "font", "media"]:
                        route.abort()
                    else:
                        route.continue_()
                page.route("**/*", block_aggressively)

                # --- OPTIMIZATION: Network Interception ---
                def handle_response(response):
                    if "generateContent" in response.url and response.status == 200:
                        try:
                            # We don't parse the whole stream here (it's complex chunks),
                            # but we use this as a signal that the backend is active.
                            pass 
                        except: pass
                page.on("response", handle_response)
                
                print("✅ [Thread] Browser Ready.")

                while True:
                    task = self.cmd_queue.get()
                    if task is None: break 
                    
                    cmd_type, data, result_queue = task
                    try:
                        if cmd_type == "prompt":
                            # Normal prompt, allow navigation/reset if needed
                            msg, use_clip = data
                            response = self._internal_send_prompt(page, msg, use_clipboard=use_clip, skip_nav=False)
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
                            if "aistudio.google.com/app" not in page.url:
                                page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="networkidle")
                            
                            # Get the list and the active one
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

            try:
                if page.locator("mat-progress-bar").is_visible():
                    print("   [Thread] Processing bar detected. Waiting...")
                    page.locator("mat-progress-bar").wait_for(state="hidden", timeout=120000)
            except:
                pass

            print("   [Thread] File attached. Sending prompt...")
            
            # 5. Send Prompt with SKIP NAV enabled so we don't refresh the page
            return self._internal_send_prompt(page, prompt, use_clipboard=False, skip_nav=True)

        except Exception as e:
            page.keyboard.press("Escape")
            return f"Upload/Extract Failed: {str(e)}"
    
    def _internal_get_markdown(self, page):
        """Clicks 'Copy as Markdown' on the last response and returns clipboard content."""
        print("   [Thread] Copying answer as Markdown...")
        try:
            # 1. Find the options button for the LAST turn
            # Targeting ms-chat-turn-options
            options_buttons = page.locator("ms-chat-turn-options button[aria-label='Open options']").all()
            if not options_buttons:
                return "Error: No chat options button found. Ensure chat has started."
            
            last_option_btn = options_buttons[-1]
            last_option_btn.scroll_into_view_if_needed()
            last_option_btn.click()
            
            # 2. Wait for the menu item 'Copy as markdown'
            # Using text filter is safer than nth-child index which can change
            copy_btn = page.locator("button.mat-mdc-menu-item").filter(has_text="Copy as markdown")
            
            if not copy_btn.is_visible():
                # Fallback: sometimes it's just 'Copy'
                print("   [Thread] 'Copy as markdown' not found, checking raw Copy...")
                copy_btn = page.locator("button.mat-mdc-menu-item").filter(has_text="Copy").first
            
            if not copy_btn.is_visible():
                page.keyboard.press("Escape")
                return "Error: Copy option not found in menu."

            # 3. Click Copy
            copy_btn.click()
            time.sleep(0.5) # Wait for clipboard write
            
            # 4. Read from clipboard
            # This requires 'clipboard-read' permission set in launch_persistent_context
            markdown_content = page.evaluate("navigator.clipboard.readText()")
            
            print(f"   [Thread] Markdown copied ({len(markdown_content)} chars).")
            return markdown_content

        except Exception as e:
            # Attempt to close menu if open
            page.keyboard.press("Escape")
            return f"Error getting markdown: {str(e)}"

    def _internal_send_prompt(self, page, message, use_clipboard=False, skip_nav=False):
        """Logic executed strictly inside the worker thread."""
        try:
            # Navigation logic depends on whether we are continuing a flow (file upload) or starting new
            if not skip_nav:
                if "aistudio.google.com" not in page.url:
                     page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="networkidle", timeout=60000)

            # Ensure prompt box is ready
            prompt_box = page.locator('textarea, [placeholder*="Start typing"]')
            prompt_box.wait_for(state="visible", timeout=15000)

            # Using fill() is faster than type() and bypasses most event delays
            prompt_box.fill(message)

            # 2. Trigger Run
            run_btn = page.locator('ms-run-button button[aria-label="Run"]')
            run_btn.click()

            print("   [Thread] AI Processing...", end="", flush=True)
            
            try:
                # First, wait for the stop button to appear (generation started)
                stop_btn = page.locator('ms-run-button button:has-text("Stop")')
                stop_btn.wait_for(state="visible", timeout=5000)
                
                # Now, wait for the stop button to DISAPPEAR (generation finished)
                stop_btn.wait_for(state="hidden", timeout=300000)
            except:
                # Fallback if the response was so fast the Stop button never registered
                pass

            # 4. Final Data Retrieval
            if use_clipboard:
                markdown = self._internal_get_markdown_via_clipboard(page)
                if markdown: return markdown

            # Final Fallback to DOM Scrape
            final_chunks = page.locator('ms-text-chunk').all()
            if not final_chunks: return "Error: No response found."
            
            # Use the last chunk and clean it
            raw_text = final_chunks[-1].inner_text().strip()
            return self._clean_ui_junk(raw_text)

        except Exception as e:
            return f"Browser Error: {str(e)}"

    def _internal_get_models(self, page):
        """Scrapes available Gemini models from the UI."""
        print("   [Thread] Fetching models...")
        # Ensure we are on the app page
        if "aistudio.google.com/app" not in page.url:
             page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="networkidle", timeout=60000)

        try:
            # Wait for the model selector to be present
            page.locator("ms-model-selector button").wait_for(state="visible", timeout=20000)
            
            # Open the menu to populate the list
            model_btn = page.locator("ms-model-selector button")
            model_btn.click()
            time.sleep(1.0) # Wait for animation
            
            # Target the model title text in the dropdown
            page.locator(".model-title-text").first.wait_for(timeout=5000)
            elements = page.locator(".model-title-text").all()
            
            models = list(dict.fromkeys([t.inner_text().strip() for t in elements if t.inner_text().strip()]))
            
            # Close menu
            page.keyboard.press("Escape")
            return models
        except Exception as e:
            print(f"   [Thread] Error fetching model list: {e}")
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

    def send_prompt(self, message, use_clipboard=False):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("prompt", (message, use_clipboard), result_queue))
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
        """
        Scrapes the clean Display Name of the active model using the 
        specific span.title inside the model selector button.
        """
        try:
            # We use a combined selector: Look for span.title specifically 
            # inside the ms-model-selector button.
            # This matches your provided path but is more resilient to small UI changes.
            selector = "ms-model-selector button span.title"
            
            model_el = page.locator(selector).first
            
            # Ensure the element is attached and visible
            model_el.wait_for(state="visible", timeout=5000)
            
            # Get the text (e.g., "Gemini 3 Flash Preview")
            text = model_el.inner_text().strip()
            
            # Final cleanup: Remove hidden characters or extra newlines
            # which sometimes appear in Angular spans
            clean_text = " ".join(text.split())
            
            print(f"   [Thread] Scraped Active Model: {clean_text}")
            return clean_text
            
        except Exception as e:
            print(f"   [Thread] Warning: Could not scrape active model name: {e}")
            
            # Fallback to the exact full path you provided if the short one fails
            try:
                full_path_selector = "body > app-root > ms-app > div > div > div.layout-wrapper > div > span > ms-prompt-renderer > ms-chunk-editor > ms-right-side-panel > div > ms-run-settings > div.settings-items-wrapper > div > ms-prompt-run-settings-switcher > ms-prompt-run-settings > div.settings-item.settings-model-selector > div > ms-model-selector > button > span.title"
                text = page.locator(full_path_selector).first.inner_text().strip()
                return " ".join(text.split())
            except:
                return None
    
    def _internal_get_markdown_via_clipboard(self, page):
        """Extracts the cleanest version of the response using AI Studio's own copy tool."""
        try:
            latest_turn = page.locator("ms-chat-turn").last
            latest_turn.hover()
            
            options_btn = latest_turn.locator("button[aria-label='Open options']")
            options_btn.click()
            
            # Wait for the specific menu item
            copy_btn = page.locator("button[role='menuitem']").filter(has_text="Copy as markdown")
            copy_btn.wait_for(state="visible", timeout=2000)
            copy_btn.click()
            
            # Instant read from the browser's shared clipboard
            return page.evaluate("navigator.clipboard.readText()")
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
    
    def get_last_response_as_markdown(self):
        """Retrieves the last AI response formatted as Markdown."""
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("get_markdown", None, result_queue))
        try:
            return result_queue.get(timeout=30)
        except queue.Empty:
            return "Error: Timeout retrieving markdown."

    def reset(self):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("reset", None, result_queue))
        result_queue.get()

browser_bridge = AIStudioBridge()