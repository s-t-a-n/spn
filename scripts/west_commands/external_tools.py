"""External tool name resolution with version fallback"""

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(__file__))
from spn_common import Logger


class ExternalTool:
    def __init__(self, candidates):
        self.candidates = candidates
        self._path = None

    def __str__(self):
        if self._path is None:
            self._path = self._find()
        return self._path

    def __fspath__(self):
        return str(self)

    def _find(self):
        preferred = self.candidates[0]
        for candidate in self.candidates:
            if shutil.which(candidate):
                if candidate != preferred:
                    Logger.wrn(f"{preferred} not found, using {candidate}")
                return candidate

        Logger.err(f'Tool not found: {", ".join(self.candidates)}')
        sys.exit(1)


CLANG_FORMAT = ExternalTool(["clang-format-20", "clang-format"])
RUN_CLANG_TIDY = ExternalTool(["run-clang-tidy-20", "run-clang-tidy"])
CPPCHECK = ExternalTool(["cppcheck"])
GCOVR = ExternalTool(["gcovr"])
