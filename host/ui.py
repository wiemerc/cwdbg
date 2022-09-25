#
# ui.py - part of CWDebug, a source-level debugger for the AmigaOS
#         This file contains the classes for the terminal UI on the host machine.
#
# Copyright(C) 2018-2022 Constantin Wiemer


from loguru import logger
from typing import Any
from urwid import AttrMap, Columns, Edit, ExitMainLoop, Filler, Frame, LineBox, MainLoop, Padding, Pile, Text

from cli import QuitDebuggerException
from debugger import dbg
from target import TargetInfo


PALETTE = [
    ('banner', 'black,bold', 'dark green'),
]

INPUT_WIDGET_HEIGHT = 15
MAX_NUM_OF_LOG_MESSAGES = 10


class UrwidHandler:
    def __init__(self, widget: Text):
        self._widget = widget
        self._messages = []

    def write(self, message: str):
        self._messages.append(message)
        if len(self._messages) > MAX_NUM_OF_LOG_MESSAGES:
            del self._messages[0]
        self._widget.set_text(self._messages)


class CommandInput(Edit):
    def __init__(self, main_screen: Any):
        super().__init__(caption='> ')
        self._main_screen = main_screen
        self._history = []

    def keypress(self, size, key):
        if key == 'enter':
            cmd_line = self.get_edit_text().strip()
            try:
                result, target_info = dbg.cli.process_command(cmd_line)
            except QuitDebuggerException:
                logger.debug("Exiting debugger...")
                raise ExitMainLoop()
            if target_info:
                self._main_screen.update_views(target_info)

            self._history.append(f"> {cmd_line}")
            if result:
                self._history.append(result)
            if len(self._history) > INPUT_WIDGET_HEIGHT:
                del self._history[0:len(self._history) - INPUT_WIDGET_HEIGHT]
            self.set_caption('\n'.join(self._history) + '\n> ')
            self.set_edit_text('')

        else:
            return super().keypress(size, key)


class MainScreen:
    def __init__(self, verbose: bool):
        def _handle_global_input(key: str):
            if key == 'f5':
                self._input_view.set_edit_text('cont')
                self._input_view.keypress(0, 'enter')
            elif key == 'f8':
                self._input_view.set_edit_text('step')
                self._input_view.keypress(0, 'enter')
            elif key == 'f10':
                self._input_view.set_edit_text('quit')
                self._input_view.keypress(0, 'enter')
            else:
                logger.error(f"Function key '{key}' not implemented")


        self._source_view = Text("Source code will be shown here...")
        source_widget = LineBox(
            Padding(
                Filler(
                    Pile([
                        Text(
                            ('banner', "Source code"),
                            align='center'
                        ),
                        self._source_view
                    ]),
                    valign='top',
                    top=1,
                    bottom=1
                ),
                left=1,
                right=1
            )
        )

        self._disasm_view = Text("Dissambled code will be shown here...")
        disasm_widget = LineBox(
            Padding(
                Filler(
                    Pile([
                        Text(
                            ('banner', "Disassembled code"),
                            align='center'
                        ),
                        self._disasm_view
                    ]),
                    valign='top',
                    top=1,
                    bottom=1
                ),
                left=1,
                right=1
            )
        )

        self._register_view = Text("Registers will be shown here...")
        register_widget = LineBox(
            Padding(
                Filler(
                    Pile([
                        Text(
                            ('banner', "Registers"),
                            align='center'
                        ),
                        self._register_view
                    ]),
                    valign='top',
                    top=1,
                    bottom=1
                ),
                left=1,
                right=1
            )
        )

        self._stack_view = Text("Stack will be shown here...")
        stack_widget = LineBox(
            Padding(
                Filler(
                    Pile([
                        Text(
                            ('banner', "Stack"),
                            align='center'
                        ),
                        self._stack_view
                    ]),
                    valign='top',
                    top=1,
                    bottom=1
                ),
                left=1,
                right=1
            )
        )

        self._input_view = CommandInput(self)
        input_widget = LineBox(
            Padding(
                Filler(
                    self._input_view,
                    valign='top',
                ),
                left=1,
                right=1
            )
        )

        self._log_view = Text("Log messages will be shown here...")
        log_widget = LineBox(
            Padding(
                Filler(
                    self._log_view,
                    valign='top',
                ),
                left=1,
                right=1
            )
        )

        title = AttrMap(Text("CWDebug - a source-level debugger for the AmigaOS", align='center'), 'banner')
        menu = AttrMap(Text("F5 = Continue, F8 = Single-step over, F10 = Quit"), 'banner')
        screen = Frame(
            header=title,
            body=Pile([
                Columns([
                    Pile([source_widget, disasm_widget]),
                    Pile([register_widget, stack_widget])
                ]),
                # 2 needs to be added for the line box
                (INPUT_WIDGET_HEIGHT + 2, input_widget),
                (MAX_NUM_OF_LOG_MESSAGES + 2, log_widget)
            ]),
            footer=menu
        )

        logger.remove()
        logger.add(UrwidHandler(self._log_view))
        logger.info("Created main screen, starting event loop")

        loop = MainLoop(screen, PALETTE, unhandled_input=_handle_global_input)
        loop.run()


    def update_views(self, target_info: TargetInfo):
        logger.debug("Updating register view")
        self._register_view.set_text(target_info.get_register_view())
        logger.debug("Updating disassembler view")
        self._disasm_view.set_text(target_info.get_disasm_view())
        logger.debug("Updating stack view")
        self._stack_view.set_text(target_info.get_stack_view())
