#pragma once

// Outcome of a cover-thumbnail generation attempt. Distinguishes a permanent, structural
// absence (no cover item in the file, or a cover present but in an unsupported format) from
// a transient failure (OOM, interrupted write, a decode that bailed, cover.img not yet
// extracted). Only StructurallyAbsent is written as a 0-byte sentinel by the generator; the
// caller decides — via a session-scoped retry counter — when a repeatedly-transient book
// earns one. Kept in its own header so callers that only forward-declare Epub (e.g.
// ReaderActivity) can name the result type without pulling in the whole Epub class.
enum class ThumbResult { Ok, TransientFail, StructurallyAbsent };
