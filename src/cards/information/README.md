# Notes about files on this directory

## markdown_notes.hpp

### How it edits and renders Markdown

The `markdown_notes.hpp` file implements the rendering of markdown by using a custom markdown renderer and integrating it into an editor and preview pane. Here’s a breakdown of how it works:

1. **Markdown Editor and Preview**:
   - The editor (`TextEditor`) is configured to support markdown syntax highlighting.
   - The live preview pane uses a custom markdown renderer to display the markdown content in real-time.

2. **Markdown Rendering**:
   - The markdown content is rendered using the `rouen::helpers::render_markdown_block` function, which takes the markdown text and a configuration for font styles.
   - The function also handles links, converting them into clickable links within the preview pane.

3. **Live Preview**:
   - The live preview pane displays the rendered markdown content, allowing users to see the formatted text as they type.

4. **Integration with Editor**:
   - The editor (`TextEditor`) is configured to support markdown syntax highlighting, including bold, italic, and code formatting.
   - The editor also handles live previews of wiki links, which are displayed as clickable links.

5. **Custom Markdown Renderer**:
   - The `rouen::helpers::render_markdown_block` function is used to render the markdown content, applying custom font styles for bold, italic, and code text.
   - It also handles external links, opening them in the default web browser if they are not markdown wiki links.

Here is a simplified overview of the markdown rendering process:

```cpp
const rouen::helpers::markdown_render_config md_config{
    .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
    .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
    .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono),
};

rouen::helpers::render_markdown_block(
    editor_.GetText(),
    md_config,
    [](const std::string& url) {
        if (url.starts_with("notes:")) {
            handle_uri(url);
        } else {
            rouen::platform::open_url(url);
        }
    }
);
```

This code sets up the markdown configuration and renders the markdown content, applying custom font styles and handling external links.

