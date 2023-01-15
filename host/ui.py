#
# ui.py - part of cwdbg, a debugger for the AmigaOS
#         This file contains the classes for the terminal UI on the host machine.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import sys
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
        # TODO: Implement readline functionality including history
        if key == 'enter':
            cmd_line = self.get_edit_text().strip()
            try:
                result = dbg.cli.process_command(cmd_line)
            except QuitDebuggerException:
                logger.debug("Exiting debugger...")
                raise ExitMainLoop()
            if dbg.target_info:
                self._main_screen.update_status_line()
                self._main_screen.update_views()

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
        def _handle_global_input(key: str) -> bool:
            if key == 'f5':
                self._input_view.set_edit_text('cont')
                self._input_view.keypress(0, 'enter')
            elif key == 'f10':
                self._input_view.set_edit_text('next')
                self._input_view.keypress(0, 'enter')
            elif key == 'f11':
                self._input_view.set_edit_text('step')
                self._input_view.keypress(0, 'enter')
            elif key == 'shift f10':
                self._input_view.set_edit_text('nexti')
                self._input_view.keypress(0, 'enter')
            elif key == 'shift f11':
                self._input_view.set_edit_text('stepi')
                self._input_view.keypress(0, 'enter')
            else:
                logger.error(f"Function key '{key}' not implemented")
            return True


        self._source_view = Text("*** NOT AVAILABLE ***")
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

        self._disasm_view = Text("*** NOT AVAILABLE ***")
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

        self._register_view = Text("*** NOT AVAILABLE ***")
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

        self._variable_view = Text("*** NOT AVAILABLE ***")
        variable_widget = LineBox(
            Padding(
                Filler(
                    Pile([
                        Text(
                            ('banner', "Variables"),
                            align='center'
                        ),
                        self._variable_view
                    ]),
                    valign='top',
                    top=1,
                    bottom=1
                ),
                left=1,
                right=1
            )
        )

        self._stack_view = Text("*** NOT AVAILABLE ***")
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

        self._call_stack_view = Text("*** NOT AVAILABLE ***")
        call_stack_widget = LineBox(
            Padding(
                Filler(
                    Pile([
                        Text(
                            ('banner', "Call Stack"),
                            align='center'
                        ),
                        self._call_stack_view
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

        self._log_view = Text("")
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

        title = AttrMap(Text("cwdbg - a debugger for the AmigaOS", align='center'), 'banner')
        self._status_line = AttrMap(
            Text("F5 = continue, F10 = next, F11 = step, Shift + F10 = nexti, Shift + F11 = stepi    Status: * Idle *"),
            'banner'
        )
        main_widget = Frame(
            header=title,
            body=Pile([
                Columns([
                    Pile([source_widget, disasm_widget]),
                    Columns([
                        Pile([register_widget, stack_widget]),
                        Pile([variable_widget, call_stack_widget])
                    ])
                ]),
                # 2 needs to be added for the line box
                (INPUT_WIDGET_HEIGHT + 2, input_widget),
                (MAX_NUM_OF_LOG_MESSAGES + 2, log_widget)
            ]),
            footer=self._status_line
        )

        logger.remove()
        logger.add(UrwidHandler(self._log_view))
        logger.info("Created main screen, starting event loop")

        loop = MainLoop(main_widget, palette=PALETTE, handle_mouse=False, unhandled_input=_handle_global_input)
        try:
            loop.run()
        except BaseException:
            logger.remove()
            logger.add(sys.stderr)
            logger.exception("INTERNAL ERROR OCCURRED:")


    def update_status_line(self):
        self._status_line.original_widget.set_text(
            f"F5 = continue, F10 = next, F11 = step, Shift + F10 = nexti, Shift + F11 = stepi    "
            f"Status: * {dbg.target_info.get_status_str()} *"
        )


    def update_views(self):
        # TODO: Introduce view classes that get the necessary information from TargetInfo, track and highlight
        #       changes and generate the content for the widgets via a render() method
        logger.debug("Updating views")
        self._source_view.set_text(dbg.target_info.get_source_view())
        self._register_view.set_text(dbg.target_info.get_register_view())
        self._disasm_view.set_text(dbg.target_info.get_disasm_view())
        self._stack_view.set_text(dbg.target_info.get_stack_view())
        self._call_stack_view.set_text(dbg.target_info.get_call_stack_view())
