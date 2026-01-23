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
                page.add_init_script("Object.defineProperty(navigator, 'webdriver', {get: () => undefined})")
                
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
            time.sleep(0.5) 
            
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
            prompt_box = page.get_by_placeholder("Start typing a prompt")
            prompt_box.wait_for(state="visible", timeout=30000)
            time.sleep(1.5)

            # Inject text
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

            time.sleep(1.0)
            
            # Fallback: Click "Run" button if Enter didn't work (sometimes focus issues)
            # The "Run" button usually has aria-label="Run"
            run_btn = page.locator('ms-run-button button[aria-label="Run"]')
            if run_btn.is_visible():
                 time.sleep(1)
                 if run_btn.is_visible():
                     print("   [Thread] Clicking Run button...")
                     run_btn.click()

            print("   [Thread] Waiting for AI response...", end="", flush=True)

            try:
                # Wait up to 120 seconds (2 mins) for the text bubble to appear
                page.locator('ms-text-chunk').last.wait_for(state="visible", timeout=120000)
            except:
                return "Error: AI took too long to start generating text."
            
            stop_btn = page.locator("ms-run-button button").filter(has_text="Stop")
            run_btn = page.locator("ms-run-button button").filter(has_text="Run")

            start_time = time.time()
            last_len = 0
            stable_count = 0

            while True:
                if time.time() - start_time > 300: # Extended timeout
                    return "Error: Timeout waiting for response."

                # Keep scrolling to trigger lazy load if needed
                current_chunks = page.locator('ms-text-chunk').all()
                current_count = len(current_chunks)
                
                if current_count > 0:
                    page.evaluate("""
                        () => {
                            const chunks = document.querySelectorAll('ms-text-chunk');
                            if (chunks.length > 0) {
                                const lastChunk = chunks[chunks.length - 1];
                                lastChunk.scrollIntoView({ block: 'end', behavior: 'instant' });
                            }
                        }
                    """)

                current_chunks = page.locator('ms-text-chunk').all()
                
                # Check busy state
                is_stopping = stop_btn.is_visible()
                is_run_ready = run_btn.is_visible()
                
                print(is_stopping, is_run_ready, end=" | ", flush=True)

                current_text = current_chunks[-1].inner_text().strip() if current_chunks else ""
                current_len = len(current_text)

                print(".", end="", flush=True)

                # Heuristic: If Run button is visible AND stop button is NOT visible AND we have text
                if is_stopping:
                    stable_count = 0 # Reset counter, we are definitely busy
                
                elif is_run_ready:
                    # Even if buttons say "Ready", we double check text stability 
                    # just in case of a UI glitch.
                    if current_len == last_len and current_len > 0:
                        stable_count += 1
                        
                        # Wait 4 ticks (4 seconds) of total stability to be safe
                        if stable_count >= 8:
                            break
                    else:
                        stable_count = 0
                else:
                    stable_count = 0
                
                last_len = current_len
                time.sleep(1)

            print("\n   [Thread] Captured.")

            if use_clipboard:
                # Use the new Clipboard logic ONLY if requested
                final_chunks = page.locator('ms-text-chunk').all()
                
                # TRY TO GET HTML AND PARSE PRE TAGS?
                # Or just use text_content() which *should* preserve whitespace if it's in a pre/code block?
                # Actually, Google AI Studio renders code blocks in Monaco or CodeMirror sometimes?
                
                # Let's try to grab the code block specifically if it exists.
                code_blocks = page.locator('code').all()
                if code_blocks:
                    # Logic to stitch text? Complex.
                    pass
                
                # SIMPLEST FIX:
                # text_content usually preserves whitespace better than inner_text for code.
                raw_answer = final_chunks[-1].text_content()

            final_chunks = page.locator('ms-text-chunk').all()
            if not final_chunks: return "Error: No response chunks found."
            
            raw_answer = final_chunks[-1].text_content()

            lines = raw_answer.split('\n')
            clean_lines = []
            skip_next = False

            for i, line in enumerate(lines):
                stripped = line.strip()
                
                # Skip UI button text
                if stripped.lower() in ['code', 'json', 'download', 'copy code', 'python', 'javascript']:
                    skip_next = True
                    continue
                
                # Skip empty lines after UI elements
                if skip_next and not stripped:
                    skip_next = False
                    continue
                
                skip_next = False
                clean_lines.append(line)
            
            raw_answer = '\n'.join(clean_lines)

            # Cleanup
            clean_answer = raw_answer
            if "Expand to view model thoughts" in clean_answer:
                clean_answer = clean_answer.split("Expand to view model thoughts")[-1]

            # 🚀 ROBUST CLEANUP: Use Regex Word Boundaries
            ui_keywords = [
                "expand_more", "expand_less", "content_copy", "share", 
                "edit", "thumb_up", "thumb_down"
            ]
            
            for junk in ui_keywords:
                # Use regex to remove ONLY standalone occurrences
                clean_answer = re.sub(rf'\b{junk}\b', '', clean_answer, flags=re.IGNORECASE)

            return clean_answer.strip()

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
        """Hovers over the last message and clicks 'Copy as markdown'."""
        print("   [Thread] Attempting 'Copy as Markdown' via Clipboard...")
        try:
            # 1. Target the last message bubble
            latest_turn = page.locator("ms-chat-turn").last
            latest_turn.scroll_into_view_if_needed()
            latest_turn.hover()
            time.sleep(0.5) 

            # 2. Click the 'Three Dots' options button
            options_btn = latest_turn.locator("button[aria-label='Open options']")
            options_btn.wait_for(state="visible", timeout=3000)
            options_btn.click()
            
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
            time.sleep(0.5) 
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