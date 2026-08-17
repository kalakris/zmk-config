// ZMK Keymap Overlay Widget for Übersicht
// Displays the keymap-drawer SVG on the desktop, kept in sync with the repo.

// Which drawing to show: "go60" or "eyelash_sofle".
const BOARD = "go60";

// Repo checkout, as a shell expression (expanded by the widget's shell).
const REPO = '"$HOME/zmk-config"';

export const refreshFrequency = 60000; // re-read the SVG every minute

// Prefer the drawing committed on the current branch's remote, so a `git push`
// (which triggers the Build and Draw workflow) shows up here without a local
// pull. The fetch is backgrounded so a slow/absent network never stalls the
// widget — its result is simply picked up on the next refresh. Falls back to
// the working-tree copy when origin has no drawing yet.
export const command = `
cd ${REPO} 2>/dev/null || exit 0
( git fetch -q origin >/dev/null 2>&1 & )
branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null)
git show "origin/$branch:keymap-drawer/${BOARD}.svg" 2>/dev/null \
  || cat "keymap-drawer/${BOARD}.svg" 2>/dev/null
`;

export const render = ({ output }) => (
  <div dangerouslySetInnerHTML={{ __html: output }} />
);

export const className = `
  position: fixed;
  bottom: 30px;
  right: 30px;
  opacity: 0.85;
  pointer-events: none;
  filter: drop-shadow(0 2px 8px rgba(0, 0, 0, 0.3));

  /* The drawing is ~2120x2114 for 10 layers in 2 columns, which is taller than
     any screen at native size — scale it to fit instead of using a fixed
     transform. Shrink these to make the overlay smaller. */
  svg.keymap {
    max-width: 96vw;
    max-height: 94vh;
    width: auto;
    height: auto;
  }
`;
