#
# ui.py - part of CWDebug, a source-level debugger for the AmigaOS
#         This file contains the classes for the terminal UI on the host machine.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import capstone

from loguru import logger
from typing import Any
from urwid import AttrMap, Columns, Edit, ExitMainLoop, Filler, Frame, LineBox, MainLoop, Padding, Pile, Text

from cli import process_cli_command, QuitDebuggerException
from debugger import TargetInfo
from serio import ServerConnection


PALETTE = [
    ('banner', 'black,bold', 'dark green'),
]

INPUT_WIDGET_HEIGHT = 10
MAX_NUM_OF_LOG_MESSAGES = 5


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
    def __init__(self, main_screen: Any, server_conn: ServerConnection):
        super().__init__(caption='> ')
        self._main_screen = main_screen
        self._server_conn = server_conn
        self._history = []

    def keypress(self, size, key):
        if key == 'enter':
            command = self.get_edit_text()
            logger.debug(f"Entered command: {command}")
            try:
                result, target_info = process_cli_command(self._server_conn, command)
            except QuitDebuggerException:
                logger.debug("Exiting debugger...")
                raise ExitMainLoop()

            self._history.append(f"> {command}")
            if result:
                self._history.append(result)
            if len(self._history) > INPUT_WIDGET_HEIGHT:
                del self._history[0:len(self._history) - INPUT_WIDGET_HEIGHT]
            self.set_caption('\n'.join(self._history) + '\n> ')
            self.set_edit_text('')

            if target_info:
                # TODO: Should we clear all views if we don't have a target_info?
                self._main_screen.update_views(target_info)
        else:
            return super().keypress(size, key)


class MainScreen:
    def __init__(self, verbose: bool, server_conn: ServerConnection):
        def _handle_global_input(key: str):
            if key == 'f10':
                raise ExitMainLoop()
            # TODO: Implement other function keys


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

        self._input_view = CommandInput(self, server_conn)
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
        menu = AttrMap(Text("F5 = Run / Continue, F8 = Step over, F9 = Step in, F10 = Quit"), 'banner')
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

        self._disassembler = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_32)

        loop = MainLoop(screen, PALETTE, unhandled_input=_handle_global_input)
        loop.run()


    def update_views(self, target_info: TargetInfo):
        logger.debug("Updating register view")
        regs = []
        for i in range(7):
            regs.append(f'A{i}=0x{target_info.task_context.reg_a[i]:08x}        D{i}=0x{target_info.task_context.reg_d[i]:08x}\n')
        regs.append(f'A7=0x{target_info.task_context.reg_sp:08x}        D7=0x{target_info.task_context.reg_d[7]:08x}\n')
        self._register_view.set_text(regs)

        logger.debug("Updating disassembler view")
        instr = next(self._disassembler.disasm(bytes(target_info.next_instr_bytes), target_info.task_context.reg_pc, 1))
        self._disasm_view.set_text(f'{instr.address:08x}:    {instr.mnemonic:<10}{instr.op_str}')

        logger.debug("Updating stack view")
        stack_dwords = []
        for i in range(8):
            stack_dwords.append(f'SP + {i * 4:02}:    0x{target_info.top_stack_dwords[i]:08x}\n')
        self._stack_view.set_text(stack_dwords)
