// lib/screens/workspace_panels.dart
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:study_assistance/provider/project_provider.dart';
import 'package:flutter_html/flutter_html.dart';
import 'package:super_clipboard/super_clipboard.dart';
import 'package:flutter_markdown/flutter_markdown.dart'; 

// Shared Constants for Styling
const Color kNotebookBg = Color(0xFFF0F4F9);
const Color kSecondaryText = Color(0xFF444746);

// --- SHARED COMPONENTS ---

class PanelHeader extends StatelessWidget {
  final String title;
  final IconData icon;
  final Color iconColor;

  const PanelHeader({super.key, required this.title, required this.icon, required this.iconColor});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.fromLTRB(20, 16, 20, 16),
      decoration: const BoxDecoration(
        color: Colors.white,
        border: Border(bottom: BorderSide(color: Color(0xFFE0E0E0), width: 0.5)),
      ),
      child: Row(
        children: [
          Icon(icon, color: iconColor, size: 22),
          const SizedBox(width: 12),
          Text(
            title,
            style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: kSecondaryText),
          ),
        ],
      ),
    );
  }
}

// --- PANEL 1: SOURCES ---

class SourcesPanel extends StatelessWidget {
  final ScrollController? scrollController;
  const SourcesPanel({super.key, this.scrollController});

  @override
  Widget build(BuildContext context) {
    final p = context.watch<ProjectProvider>();
    final sources = p.sources;
    final selected = p.selectedSource;

    return Container(
      color: Colors.white,
      child: Column(
        children: [
          PanelHeader(title: "Sources", icon: Icons.folder_outlined, iconColor: Colors.blue[700]!),
          Expanded(
            child: p.isLoadingSources
                ? const Center(child: CircularProgressIndicator(strokeWidth: 2))
                : ListView.builder(
                    controller: scrollController,
                    padding: const EdgeInsets.all(16),
                    itemCount: sources.length + 1,
                    itemBuilder: (ctx, i) {
                      if (i == 0) {
                        return _SourceTile(
                          title: 'All Sources',
                          icon: Icons.all_inclusive_rounded,
                          isSelected: selected == null,
                          onTap: () => p.selectSource(null),
                          color: const Color(0xFFF3E5F5), // Light Purple
                        );
                      }
                      final s = sources[i - 1];
                      return _SourceTile(
                        title: s.filename,
                        icon: Icons.description_outlined,
                        isSelected: s.id == selected?.id,
                        isDeleting: s.id == p.deletingSourceId,
                        onTap: () => p.selectSource(s),
                        onDelete: () => _showDeleteSourceConfirmDialog(context, p, s),
                        color: i % 2 == 0 ? const Color(0xFFE3F2FD) : const Color(0xFFE8F5E9),
                      );
                    },
                  ),
          ),
          _buildUploadFooter(p, sources),
        ],
      ),
    );
  }

  Widget _buildUploadFooter(ProjectProvider p, List<Source> s) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: const BoxDecoration(
        color: Colors.white,
        border: Border(top: BorderSide(color: Color(0xFFE0E0E0), width: 0.5)),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          ElevatedButton.icon(
            onPressed: p.isUploading ? null : p.pickAndUploadFiles,
            icon: p.isUploading
                ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white))
                : const Icon(Icons.add, size: 20),
            label: Text(p.isUploading ? "Uploading..." : "Add sources"),
            style: ElevatedButton.styleFrom(
              backgroundColor: Colors.black,
              foregroundColor: Colors.white,
              minimumSize: const Size(double.infinity, 52),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(26)),
              elevation: 0,
            ),
          ),
          if (s.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(top: 12),
              child: Text("${s.length} total sources", style: const TextStyle(fontSize: 12, color: Colors.grey)),
            ),
        ],
      ),
    );
  }

  void _showDeleteSourceConfirmDialog(BuildContext context, ProjectProvider p, Source s) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
        title: const Text('Remove Source?'),
        content: Text('Delete "${s.filename}" from this project?'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          ElevatedButton(
            style: ElevatedButton.styleFrom(backgroundColor: Colors.red, foregroundColor: Colors.white, elevation: 0),
            onPressed: () { Navigator.pop(ctx); p.deleteSource(s.id); },
            child: const Text('Delete'),
          ),
        ],
      ),
    );
  }
}

class _SourceTile extends StatelessWidget {
  final String title;
  final IconData icon;
  final bool isSelected;
  final bool isDeleting;
  final VoidCallback onTap;
  final VoidCallback? onDelete;
  final Color color;

  const _SourceTile({
    required this.title,
    required this.icon,
    this.isSelected = false,
    this.isDeleting = false,
    required this.onTap,
    this.onDelete,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8.0),
      child: InkWell(
        onTap: isDeleting ? null : onTap,
        borderRadius: BorderRadius.circular(12),
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 200),
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
          decoration: BoxDecoration(
            color: isSelected ? color : Colors.white,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: isSelected ? color : const Color(0xFFE0E0E0)),
          ),
          child: Row(
            children: [
              Container(
                padding: const EdgeInsets.all(8),
                decoration: BoxDecoration(color: Colors.white.withOpacity(0.5), borderRadius: BorderRadius.circular(8)),
                child: Icon(icon, color: kSecondaryText, size: 20),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Text(
                  title,
                  style: TextStyle(
                    fontWeight: isSelected ? FontWeight.bold : FontWeight.w400,
                    fontSize: 14,
                    color: Colors.black87,
                  ),
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
              if (isDeleting)
                const SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2))
              else if (onDelete != null)
                IconButton(
                  icon: const Icon(Icons.more_vert, size: 18),
                  onPressed: onDelete,
                  padding: EdgeInsets.zero,
                  constraints: const BoxConstraints(),
                )
            ],
          ),
        ),
      ),
    );
  }
}

// --- PANEL 2: AI CHAT ---

class AiChatPanel extends StatelessWidget {
  const AiChatPanel({super.key});

  @override
  Widget build(BuildContext context) {
    final p = context.watch<ProjectProvider>();
    final chat = p.chatHistory;
    final thinking = p.isBotThinking;

    return Container(
      color: Colors.white,
      child: Column(
        children: [
          // --- CUSTOM HEADER WITH DROPDOWN ---
          _buildChatHeader(context, p),
          
          Expanded(
            child: ListView.builder(
              padding: const EdgeInsets.all(20),
              itemCount: chat.length + (thinking ? 1 : 0),
              itemBuilder: (ctx, i) {
                if (i == chat.length && thinking) return _buildChatBubble(context, "...", false);
                final msg = chat[i];
                return _buildChatBubble(context, msg.content, msg.isUser);
              },
            ),
          ),
          _buildChatInput(p),
        ],
      ),
    );
  }

  Widget _buildChatHeader(BuildContext context, ProjectProvider p) {
    return Container(
      padding: const EdgeInsets.fromLTRB(20, 12, 20, 12),
      decoration: const BoxDecoration(
        color: Colors.white,
        border: Border(bottom: BorderSide(color: Color(0xFFE0E0E0), width: 0.5)),
      ),
      child: Row(
        children: [
          const Icon(Icons.auto_awesome_outlined, color: Colors.purple, size: 22),
          const SizedBox(width: 12),
          const Text(
            "AI Chat",
            style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: kSecondaryText),
          ),
          const Spacer(),
          
          // --- MODEL SELECTOR ---
          if (p.isLoadingModels)
            const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
          else if (p.availableModels.isNotEmpty)
            Container(
              height: 32,
              padding: const EdgeInsets.symmetric(horizontal: 12),
              decoration: BoxDecoration(
                color: Colors.purple.shade50,
                borderRadius: BorderRadius.circular(16),
                border: Border.all(color: Colors.purple.shade100),
              ),
              child: DropdownButtonHideUnderline(
                child: DropdownButton<String>(
                  value: p.availableModels.contains(p.currentModel) 
                      ? p.currentModel 
                      : p.availableModels.first, 
                  icon: const Icon(Icons.arrow_drop_down, color: Colors.purple, size: 18),
                  style: TextStyle(fontSize: 12, color: Colors.purple.shade900, fontWeight: FontWeight.bold),
                  isDense: true,
                  onChanged: (String? newValue) {
                    if (newValue != null) {
                      p.changeModel(newValue);
                    }
                  },
                  items: p.availableModels.map<DropdownMenuItem<String>>((String value) {
                    return DropdownMenuItem<String>(
                      value: value,
                      child: Text(value),
                    );
                  }).toList(),
                ),
              ),
            )
          else
            IconButton(
              icon: const Icon(Icons.refresh, size: 18, color: Colors.grey),
              tooltip: "Load Models",
              onPressed: () => p.fetchAvailableModels(),
            ),
        ],
      ),
    );
  }

  Widget _buildChatBubble(BuildContext context, String text, bool isUser) {
    return Align(
      alignment: isUser ? Alignment.centerRight : Alignment.centerLeft,
      child: Container(
        margin: const EdgeInsets.only(bottom: 16),
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        constraints: BoxConstraints(maxWidth: MediaQuery.of(context).size.width * 0.7),
        decoration: BoxDecoration(
          color: isUser ? kNotebookBg : Colors.white,
          borderRadius: BorderRadius.only(
            topLeft: const Radius.circular(16),
            topRight: const Radius.circular(16),
            bottomLeft: Radius.circular(isUser ? 16 : 4),
            bottomRight: Radius.circular(isUser ? 4 : 16),
          ),
          border: isUser ? null : Border.all(color: const Color(0xFFE0E0E0)),
          boxShadow: isUser ? [] : [
            BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 5, offset: const Offset(0, 2))
          ],
        ),
        child: isUser 
          ? Text(text, style: const TextStyle(color: Colors.black87, fontSize: 14)) 
          : MarkdownBody(
              data: text,
              selectable: true,
              styleSheet: MarkdownStyleSheet.fromTheme(Theme.of(context)).copyWith(
                p: const TextStyle(fontSize: 14, height: 1.5, color: Colors.black87),
                strong: const TextStyle(fontWeight: FontWeight.bold, color: Colors.black),
                code: const TextStyle(backgroundColor: Color(0xFFF5F7FA), fontFamily: 'monospace', fontSize: 13),
                codeblockDecoration: BoxDecoration(
                  color: const Color(0xFFF5F7FA), 
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(color: Colors.grey.shade200)
                ),
              ),
            ),
      ),
    );
  }

  Widget _buildChatInput(ProjectProvider p) {
    return Container(
      padding: const EdgeInsets.fromLTRB(20, 0, 20, 20),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 16),
        decoration: BoxDecoration(
          color: kNotebookBg,
          borderRadius: BorderRadius.circular(28),
        ),
        child: Row(
          children: [
            Expanded(
              child: TextField(
                controller: p.chatController,
                decoration: const InputDecoration(
                  hintText: "Ask a question...",
                  border: InputBorder.none,
                  hintStyle: TextStyle(fontSize: 14),
                ),
                onSubmitted: (_) => _sendChat(p),
              ),
            ),
            IconButton(
              icon: const Icon(Icons.arrow_upward_rounded, color: Colors.black),
              onPressed: () => _sendChat(p),
            ),
          ],
        ),
      ),
    );
  }

  void _sendChat(ProjectProvider p) {
    if (p.chatController.text.trim().isNotEmpty) {
      p.askQuestion(p.chatController.text.trim());
      p.chatController.clear();
    }
  }
}

// --- PANEL 3: STUDY NOTE ---

class NotesPanel extends StatelessWidget {
  const NotesPanel({super.key});

  @override
  Widget build(BuildContext context) {
    final p = context.watch<ProjectProvider>();
    final html = p.scratchpadContent;

    return Container(
      color: Colors.white,
      child: Column(
        children: [
          const PanelHeader(title: "Study Note", icon: Icons.edit_note_rounded, iconColor: Colors.orange),
          Expanded(
            child: p.isLoadingNote
                ? _buildLoadingState()
                : SingleChildScrollView(
                    padding: const EdgeInsets.all(24),
                    child: SelectionArea(
                      child: Container(
                        padding: const EdgeInsets.all(20),
                        decoration: BoxDecoration(
                          color: Colors.white,
                          borderRadius: BorderRadius.circular(16),
                          border: Border.all(color: const Color(0xFFE0E0E0)),
                        ),
                        child: Html(data: html),
                      ),
                    ),
                  ),
          ),
          _buildActionFooter(context, p, html),
        ],
      ),
    );
  }

  Widget _buildLoadingState() {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const CircularProgressIndicator(strokeWidth: 2),
          const SizedBox(height: 16),
          Text("Generating note...", style: TextStyle(color: Colors.grey[600])),
        ],
      ),
    );
  }

  Widget _buildActionFooter(BuildContext context, ProjectProvider p, String html) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: const BoxDecoration(
        color: Colors.white,
        border: Border(top: BorderSide(color: Color(0xFFE0E0E0), width: 0.5)),
      ),
      child: Column(
        children: [
          Row(
            children: [
              Expanded(
                child: _SmallActionBtn(
                  icon: Icons.refresh_rounded, 
                  label: "Reload", 
                  onTap: p.selectedSource == null ? null : () => p.getNoteForSelectedSource(),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: _SmallActionBtn(
                  icon: Icons.copy_rounded, 
                  label: "Copy", 
                  onTap: () => _copyRichTextToClipboard(context, html),
                ),
              ),
            ],
          ),
          const SizedBox(height: 16),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            decoration: BoxDecoration(color: kNotebookBg, borderRadius: BorderRadius.circular(12)),
            child: TextField(
              controller: p.topicController,
              decoration: const InputDecoration(
                hintText: "Custom topic...",
                border: InputBorder.none,
                hintStyle: TextStyle(fontSize: 14),
              ),
            ),
          ),
          const SizedBox(height: 12),
          ElevatedButton(
            onPressed: () {
              if (p.topicController.text.trim().isNotEmpty) {
                p.generateTopicNote(p.topicController.text.trim());
                FocusScope.of(context).unfocus();
              }
            },
            style: ElevatedButton.styleFrom(
              backgroundColor: Colors.blueAccent[700],
              foregroundColor: Colors.white,
              minimumSize: const Size(double.infinity, 48),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
              elevation: 0,
            ),
            child: const Text("Generate Custom Note", style: TextStyle(fontWeight: FontWeight.bold)),
          ),
        ],
      ),
    );
  }

  // --- Utility functions ---
  Future<void> _copyRichTextToClipboard(BuildContext context, String html) async {
    final item = DataWriterItem();
    item.add(Formats.htmlText(html));
    item.add(Formats.plainText(html.replaceAll(RegExp(r"<[^>]*>"), "")));
    await SystemClipboard.instance?.write([item]);
    ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text("Copied!"), behavior: SnackBarBehavior.floating));
  }
}

class _SmallActionBtn extends StatelessWidget {
  final IconData icon;
  final String label;
  final VoidCallback? onTap;

  const _SmallActionBtn({required this.icon, required this.label, this.onTap});

  @override
  Widget build(BuildContext context) {
    return OutlinedButton.icon(
      onPressed: onTap,
      icon: Icon(icon, size: 18),
      label: Text(label),
      style: OutlinedButton.styleFrom(
        foregroundColor: kSecondaryText,
        side: const BorderSide(color: Color(0xFFE0E0E0)),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
        padding: const EdgeInsets.symmetric(vertical: 12),
      ),
    );
  }
}