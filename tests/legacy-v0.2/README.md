# Historical v0.2 GUI checks

These scripts are retained as historical evidence for the v0.2 controller and
dashboard-style settings UI. They are **not current acceptance tests** and must
not be executed against v0.3 or later.

The old assertions depend on the six-entry app wheel, pointer-centered placement
with edge clamping, the previous dashboard controls, and the earlier cold-start
behavior. The current application uses nested radial menus, displaced orbit
placement, a preferred-agent tap action, and updated activation ordering.

The archived files are preserved without rewriting their assumptions or relative
paths. Their original repository-root calculations are intentionally not runnable
from this archive directory. Do not add this directory to automated test discovery.

- `run-integration.py`: former controller and launcher integration checks.
- `run-placement.py`: former pointer and work-area placement checks.
- `run-edges.py`: former screen-edge and corner release checks.
- `run-localization.py`: former dashboard language-control checks.

The current native GUI integration flow is `tools/test.ps1` with
[`../run-reel-seam.py`](../run-reel-seam.py); [`../run-radial.py`](../run-radial.py)
is 0.3.4 evidence. The current C++ tests remain in the parent `tests` directory and cover
geometry, gesture ordering, menu action indices, persistence, and configuration.
