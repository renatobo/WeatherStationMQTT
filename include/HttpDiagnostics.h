#pragma once

// Project modifications by Renato Bonomini (renatobo); MIT, see LICENSE.
// Read-only HTTP routes. beginHTTP/serviceHTTP become no-ops when the
// internal server is not compiled; callers need no matching preprocessor guards.

void beginHTTP();
void serviceHTTP();
