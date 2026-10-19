# SPDX-License-Identifier: GPL-3.0-only
"""The page icon (docs/brand), as data URIs, put in place of __RAVEL_ICON__ and __RAVEL_TOUCH__ in a page:
the emulator, the manual and the installer carry their icon inside them, so they show it offline too."""
import base64
from pathlib import Path

BRAND = Path(__file__).resolve().parents[1] / "docs" / "brand"


def apply(page):
    for mark, name in (("__RAVEL_ICON__", "ravel-icon-64.png"), ("__RAVEL_TOUCH__", "apple-touch-icon.png")):
        uri = "data:image/png;base64," + base64.b64encode((BRAND / name).read_bytes()).decode()
        page = page.replace(mark, uri)
    return page
