from textual.app import App, ComposeResult
from textual.widgets import Footer, Header, TextArea, Label, Button, Static, Footer
from rich.style import Style
from textual.widgets.text_area import TextAreaTheme
from textual import events
from textual.containers import Horizontal, VerticalScroll
# from tree_sitter_languages import get_language
import pyfiglet
from textual.binding import Binding

monakai = TextAreaTheme.get_builtin_theme("monakai")
#lua_lang = get_language("lua")

class ExtendedTextArea(TextArea):
    def _on_key(self, event: events.Key) -> None:
        if event.character == "(":
            self.insert("()")
            self.move_cursor_relative(columns=-1)
            event.prevent_default()



class MainActivity(App):
    CSS_PATH = "OSBuild.tcss"

    def compose(self) -> ComposeResult:
        title = pyfiglet.figlet_format("GothRTOS System Builder", font="slant")

        yield Label(title)
        yield Horizontal(
            Button("Get Started", variant="primary"),
            Button("Board/Port list", disabled=True),
            Button.error("Close", id="CloseAppBtn")
        )
#        yield Footer()
app = MainActivity()

if __name__ == "__main__":
    app.run()
