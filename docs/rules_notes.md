# Gungi Rules Notes

This engine targets the official physical Gungi Basic Rules first edition, with a compact
C99 API that does not depend on raylib.

Implementation choices:

- Coordinates are zero-based: `x = 0..8`, `y = 0..8`. Black starts near `y = 0` and
  moves forward toward larger `y`; White starts near `y = 8` and moves forward toward
  smaller `y`.
- The default setup is deterministic and leaves seven pieces in hand for each side. It is
  a practical fixed setup, not an implementation of the alternating initial-placement phase.
- Captured pieces are removed from play and are not added to either player's hand.
- "New" / Arata drops may be placed on empty squares or on the player's own non-Marshal
  top piece, from that player's edge through their most advanced occupied row.
- Only the top piece of a stack can move. A stack can be at most three pieces high.
- A piece can stack on friendly or enemy pieces when the destination stack is not taller
  than the moving piece's source stack. Pieces cannot stack on a Marshal.
- Capturing an enemy stack removes all enemy pieces in that destination stack and then
  places the moving piece on the remaining friendly pieces, matching the physical rulebook
  examples.
- Marshal capture, resignation, and the fourth occurrence of the same position end the game.
  Repetition includes board contents, side to move, stack order, and hands.
- Check detection finds whether the current top Marshal is capturable by an opponent top
  piece. Moves that leave the mover's Marshal in check are rejected when that Marshal exists.
- Checkmate search is not implemented yet; `RulesResult.checkmate` remains `0`.
- Captain turncoat replacement and any optional advanced/variant rules are not implemented.

Movement table notes:

- Movement is data-table driven in `src/gungi_rules.c`.
- Ordinary pieces cannot jump over occupied path squares.
- Cannon, Musketeer, and Archer jump moves may pass over occupied stacks whose height is
  less than or equal to the moving piece's source stack height; taller stacks block them.
- Non-unlimited movement directions extend by stack height: level 2 adds one square and
  level 3 adds two squares. General and Lieutenant General unlimited red directions do not
  change with height.

