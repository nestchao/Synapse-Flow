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
                        "--disable-blink-features=AutomationControlled",
                        "--disable-gpu",
                        "--disable-software-rasterizer",
                        "--disable-extensions",
                        "--disable-background-networking",
                        "--disable-sync",
                        "--disable-default-apps",
                        "--disable-translate",
                        "--disable-notifications",
                        "--disable-dev-shm-usage",
                        "--no-sandbox",
                        "--mute-audio",
                        "--js-flags='--max-old-space-size=256'",  # Reduced memory
                        "--disable-features=IsolateOrigins,site-per-process"  # Reduces memory
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

                # Disable all CSS animations and transitions for speed
                page.add_init_script("""
                    const style = document.createElement('style');
                    style.innerHTML = `
                        *, *::before, *::after {
                            transition: none !important;
                            animation: none !important;
                        }
                    `;
                    document.head.appendChild(style);
                """)
                
                page.add_init_script("Object.defineProperty(navigator, 'webdriver', {get: () => undefined})")
                
                print("✅ [Thread] Browser Ready.")

                while True:
                    task = self.cmd_queue.get()
                    if task is None: break 
                    
                    cmd_type, data, result_queue = task
                    try:
                        if cmd_type == "prompt":
                            # Normal prompt, allow navigation/reset if needed
                            # CHANGED: use_clip -> return_markdown
                            msg, return_markdown = data 
                            # CHANGED: use_clipboard -> return_markdown
                            response = self._internal_send_prompt(page, msg, return_markdown=return_markdown, skip_nav=False) 
                            result_queue.put(response)
                        
                        elif cmd_type == "upload_extract":
                            # data is tuple: (file_path, prompt)
                            file_path, prompt = data
                            response = self._internal_upload_and_extract(page, file_path, prompt)
                            result_queue.put(response)

                        elif cmd_type == "reset":
                            page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded")
                            result_queue.put(True)

                        elif cmd_type == "get_state":
                            if "aistudio.google.com/app" not in page.url:
                                page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded")
                            
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
                 page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded", timeout=60000)
            else:
                 # Check if the "New chat" button is visible and click it, otherwise reload
                 # This is safer than just reloading if the URL is generic
                 page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded")
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
            # REPLACED: time.sleep(0.5) with implicit wait via expect_file_chooser
            
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
            # REPLACED: time.sleep(1) with immediate check/wait
            try:
                progress_bar = page.locator("mat-progress-bar")
                if progress_bar.is_visible():
                    print("   [Thread] Processing bar detected. Waiting...")
                    progress_bar.wait_for(state="hidden", timeout=120000)
            except:
                pass

            print("   [Thread] File attached. Sending prompt...")
            
            # 5. Send Prompt with SKIP NAV enabled so we don't refresh the page
            # Set return_markdown=False as extraction usually requires raw text
            return self._internal_send_prompt(page, prompt, return_markdown=False, skip_nav=True)

        except Exception as e:
            page.keyboard.press("Escape")
            return f"Upload/Extract Failed: {str(e)}"
    
    # REMOVED: _internal_get_markdown (was duplicate/old)

    def _internal_send_prompt(self, page, message, return_markdown=False, skip_nav=False):
        """Optimized prompt sending with faster detection."""
        try:
            # Only navigate if absolutely necessary
            if not skip_nav:
                print("   [Thread] 🔄 Forcing New Chat for fresh context...", flush=True)
                page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded", timeout=60000)

            # Wait for prompt box with shorter timeout
            prompt_box = page.get_by_placeholder("Start typing a prompt")
            prompt_box.wait_for(state="visible", timeout=15000)

            # Inject text directly (no sleep needed)
            page.evaluate("""
                (text) => {
                    const el = document.querySelector('textarea, [placeholder*="Start typing"]');
                    if (el) {
                        el.value = text;
                        el.dispatchEvent(new Event('input', { bubbles: true }));
                        el.dispatchEvent(new Event('change', { bubbles: true }));
                    }
                }
            """, message)

            # Click Run button immediately (remove the 1s sleep)
            run_btn = page.locator('ms-run-button button').filter(has_text="Run")
            run_btn.wait_for(state="visible", timeout=5000)
            run_btn.click()
            # self._force_scroll_bottom(page)

            print("   [Thread] Waiting for AI response...", flush=True)

            # Wait for first response chunk
            # page.locator('ms-text-chunk').first.wait_for(state="visible", timeout=240000)
            
            run_btn_ready = page.locator("ms-run-button button").filter(has_text="Run")
            
            # --- Stability Settings ---
            last_text_len = 0
            stability_counter = 0
            required_stability = 4  # Wait for 4 consecutive stable checks (approx 2-4 seconds)
            max_checks = 240        # Loop limit (approx 120 seconds max wait)
            
            for i in range(max_checks):
                # --- SCROLL FIX (User Provided) ---
                # This recursively finds the scrollable parent and forces it down
                page.evaluate("""
                    () => {
                        const chunks = document.querySelectorAll('ms-text-chunk');
                        if (chunks.length > 0) {
                            const lastChunk = chunks[chunks.length - 1];
                            
                            // 1. Direct Element Scroll
                            lastChunk.scrollIntoView({ block: 'end', behavior: 'instant' });
                            
                            // 2. Recursive Parent Scroll (The 'Nuclear' Option)
                            let parent = lastChunk.parentElement;
                            while (parent) {
                                // If this parent is scrollable (content bigger than view)
                                if (parent.scrollHeight > parent.clientHeight) {
                                    parent.scrollTop = parent.scrollHeight;
                                }
                                parent = parent.parentElement;
                            }
                            
                            // 3. Specific UI Element Fallback
                            const editor = document.querySelector('ms-prompt-editor');
                            if (editor) editor.scrollTop = editor.scrollHeight;
                        }
                    }
                """)
                
                # Wait a moment for render
                page.wait_for_timeout(500)

                # Get the VERY LAST chat turn for checking length
                # We use evaluate to ensure we get the browser's rendered state
                current_text = page.evaluate("""
                    () => {
                        const turns = document.querySelectorAll('ms-chat-turn');
                        return turns.length > 0 ? turns[turns.length - 1].innerText : "";
                    }
                """)
                
                current_len = len(current_text)

                # Check if "Run" button is visible (meaning generation likely stopped)
                is_run_visible = run_btn_ready.is_visible()

                if is_run_visible and current_len > 0:
                    if current_len == last_text_len:
                        stability_counter += 1
                        # print(f"   [Debug] Stability {stability_counter}/{required_stability}")
                        
                        if stability_counter >= required_stability:
                            print("   [Thread] ✅ Text stable and generation complete.")
                            break
                    else:
                        # Text changed (stream is still active)
                        stability_counter = 0 
                else:
                    # Run button not visible (still generating) or text empty
                    stability_counter = 0

                last_text_len = current_len

            print("   [Thread] Response captured.", flush=True)

            response = None
            if return_markdown:
                # User wants Markdown, try to get it cleanly via clipboard
                response = self._internal_get_markdown_via_clipboard(page)
            else:
                # User wants plain text, use 'Copy as text' function
                response = self._internal_get_text_via_clipboard(page)
                
            if response is not None:
                # page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded", timeout=60000)
                return response

            # FALLBACK: If clipboard copy failed for any reason, scrape the raw text content.
            print("   [Thread] Clipboard copy failed. Falling back to raw text scrape.")
            final_chunk = page.locator('ms-text-chunk').last
            raw_answer = final_chunk.text_content()

            # Simplified cleanup (faster regex)
            clean_answer = re.sub(r'\b(expand_more|expand_less|content_copy|share|edit|thumb_up|thumb_down|Code|JSON|Download|Copy code|Python|JavaScript)\b', '', raw_answer, flags=re.IGNORECASE)
            
            if "Expand to view model thoughts" in clean_answer:
                clean_answer = clean_answer.split("Expand to view model thoughts", 1)[-1]

            # page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded", timeout=60000)

            return clean_answer.strip()

        except Exception as e:
            return f"Browser Error: {str(e)}"

    def _internal_get_models(self, page):
        """Scrapes available Gemini models from the UI."""
        print("   [Thread] Fetching models...")
        # Ensure we are on the app page
        if "aistudio.google.com/app" not in page.url:
             page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded", timeout=60000)

        try:
            # Wait for the model selector to be present
            model_btn = page.locator("ms-model-selector button")
            model_btn.wait_for(state="visible", timeout=20000)
            
            # Open the menu to populate the list
            model_btn.click()
            # REPLACED: time.sleep(1.0) with explicit wait for the first model title in the dropdown
            
            # Target the model title text in the dropdown
            model_title_text = page.locator(".model-title-text").first
            model_title_text.wait_for(timeout=5000)
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
             page.goto("https://aistudio.google.com/app/prompts/new_chat", wait_until="domcontentloaded", timeout=60000)

        try:
            model_btn = page.locator("ms-model-selector button")
            model_btn.wait_for(state="visible", timeout=30000)
            
            # Click to open the model selection dropdown
            if not model_btn.is_visible():
                page.get_by_label("Run settings").click()
                page.wait_for_timeout(200) # Small wait for settings panel to open
            model_btn.click()
            
            target = page.locator(".model-title-text").get_by_text(model_name, exact=True).first
            target.wait_for(state="visible", timeout=5000) # Wait for target to be visible

            try:
                gemini_filter = page.locator("button.ms-button-filter-chip").filter(has_text="Gemini").first
                if gemini_filter.is_visible(): 
                    gemini_filter.click()
                    target.wait_for(state="visible", timeout=5000) # Wait for target to be visible after filtering
            except: pass

            target.click()
            # REPLACED: time.sleep(1.0) with wait for the selection menu to hide/close
            page.locator("mat-mdc-menu-panel").first.wait_for(state="hidden", timeout=5000)

            return True
        except Exception as e:
            page.keyboard.press("Escape")
            return f"Error: {e}"

    def send_prompt(self, message, return_markdown=False):
        """
        Sends a prompt to AI Studio.
        
        :param message: The prompt text.
        :param return_markdown: If True, attempts to retrieve the response 
                                as Markdown via 'Copy as markdown'.
                                If False, attempts to retrieve as plain text 
                                via 'Copy as text'.
        """
        self.start()
        result_queue = queue.Queue()
        # CHANGED: use_clipboard -> return_markdown
        self.cmd_queue.put(("prompt", (message, return_markdown), result_queue)) 
        try:
            # Increased timeout slightly for reliable thread communication
            return result_queue.get(timeout=300) 
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
        """Hovers over the last message and clicks 'Copy as markdown'."""
        print("   [Thread] Attempting 'Copy as Markdown' via Clipboard...")
        try:
            # 1. Target the last message bubble
            latest_turn = page.locator("ms-chat-turn").last
            latest_turn.scroll_into_view_if_needed()
            latest_turn.hover()
            
            # 2. Click the 'Three Dots' options button
            options_btn = latest_turn.locator("button[aria-label='Open options']")
            # Explicit wait for options button to appear after hover (REMOVED: time.sleep(0.5) equivalent)
            options_btn.wait_for(state="visible", timeout=3000) 
            options_btn.click(force=True) 
            
            # 3. Find and Click 'Copy as markdown'
            # We look for the menu item containing the specific text
            copy_btn = page.locator("button[role='menuitem']").filter(has_text="Copy as markdown")
            
            if not copy_btn.is_visible():
                # Fallback: Try generic class selector if role attribute is missing
                copy_btn = page.locator("button.mat-mdc-menu-item").filter(has_text="Copy as markdown")

            if not copy_btn.is_visible():
                print("   [Thread] 'Copy as markdown' option not found in menu.")
                page.keyboard.press("Escape")
                return None

            copy_btn.wait_for(state="visible", timeout=2000)
            copy_btn.click()
            
            # 4. Read Clipboard
            # A small timeout is often unavoidable here as Playwright needs 
            # to wait for the OS/Browser to populate the clipboard after the click.
            page.wait_for_timeout(200) 
            clipboard_text = page.evaluate("navigator.clipboard.readText()")
            
            # Close menu just in case
            page.keyboard.press("Escape")
            
            print(f"   [Thread] Clipboard Copy Successful ({len(clipboard_text)} chars).")
            return clipboard_text

        except Exception as e:
            print(f"   [Thread] ⚠️ Copy as Markdown failed: {e}")
            try:
                page.keyboard.press("Escape")
            except: pass
            return None
    
    def _internal_get_text_via_clipboard(self, page):
        """Hovers over the last message and clicks 'Copy as text'."""
        print("   [Thread] Attempting 'Copy as text' via Clipboard...")
        try:
            # 1. Target the last message bubble
            latest_turn = page.locator("ms-chat-turn").last
            latest_turn.scroll_into_view_if_needed()
            latest_turn.hover()
            # REPLACED: time.sleep(0.5) with explicit wait for options button

            # 2. Click the 'Three Dots' options button
            options_btn = latest_turn.locator("button[aria-label='Open options']")
            options_btn.wait_for(state="visible", timeout=3000)
            options_btn.click(force=True) 
            
            # 3. Find and Click 'Copy as text'
            copy_btn = page.locator("button[role='menuitem']").filter(has_text="Copy as text")
            
            if not copy_btn.is_visible():
                # Fallback: Try generic class selector if role attribute is missing
                copy_btn = page.locator("button.mat-mdc-menu-item").filter(has_text="Copy as text")

            if not copy_btn.is_visible():
                print("   [Thread] 'Copy as text' option not found in menu.")
                page.keyboard.press("Escape")
                return None

            copy_btn.wait_for(state="visible", timeout=2000)
            copy_btn.click()
            
            # 4. Read Clipboard
            # REPLACED: time.sleep(0.5) with Playwright timeout
            page.wait_for_timeout(200) 
            clipboard_text = page.evaluate("navigator.clipboard.readText()")
            
            # Close menu just in case
            page.keyboard.press("Escape")
            
            print(f"   [Thread] Clipboard Text Copy Successful ({len(clipboard_text)} chars).")
            return clipboard_text

        except Exception as e:
            print(f"   [Thread] ⚠️ Copy as Text failed: {e}")
            try:
                page.keyboard.press("Escape")
            except: pass
            return None

    # REMOVED: _internal_get_answer_via_clipboard (was confusing/duplicate)
    
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
        # This command type 'get_markdown' is not handled in _browser_loop
        print("Warning: get_last_response_as_markdown is an unhandled command type and should be removed or re-implemented.")
        return "Error: Function not implemented or unhandled command type."

    def reset(self):
        self.start()
        result_queue = queue.Queue()
        self.cmd_queue.put(("reset", None, result_queue))
        result_queue.get()

browser_bridge = AIStudioBridge()
