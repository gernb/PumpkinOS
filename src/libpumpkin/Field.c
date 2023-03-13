#include <PalmOS.h>

static void nop(Err err);
#define PALMOS_MODULE "Field"
#define PALMOS_MODERR nop

#include "sys.h"
#include "thread.h"
#include "pwindow.h"
#include "vfs.h"
#include "pumpkin.h"
#include "debug.h"
#include "xalloc.h"

/*
  UIFieldBackground:
  UIFieldText:
  UIFieldTextLines:
  UIFieldCaret:
  UIFieldTextHighlightBackground:
  UIFieldTextHighlightForeground:

  UInt16 numeric        :1; // Set if numeric, digits and secimal separator only
  UInt16 hasScrollBar   :1; // Set if the field has a scroll bar
  UInt16 autoShift      :1; // Set if auto case shift
  UInt16 justification  :2; // text alignment
  UInt16 underlined     :2; // text underlined mode
  UInt16 dirty          :1; // Set if user modified
  UInt16 insPtVisible   :1; // Set if the ins pt is scolled into view
  UInt16 dynamicSize    :1; // Set if height expands as text is entered
  UInt16 hasFocus       :1; // Set if the field has the focus
  UInt16 singleLine     :1; // Set if only a single line is displayed
  UInt16 editable       :1; // Set if editable
  UInt16 visible        :1; // Set if drawn, used internally
  UInt16 usable         :1; // Set if part of ui

  typedef struct LineInfoTag {
    UInt16  start;      // position in text string of first char.
    UInt16  length;     // number of character in the line
  } LineInfoType;

  UInt16        id;
  RectangleType rect;
  FieldAttrType attr;
  Char          *text;          // pointer to the start of text string
  MemHandle     textHandle;       // block the contains the text string
  LineInfoPtr   lines;
  UInt16        textLen;
  UInt16        textBlockSize;
  UInt16        maxChars;
  UInt16        selFirstPos;
  UInt16        selLastPos;
  UInt16        insPtXPos;
  UInt16        insPtYPos;
  FontID        fontID;
  UInt8         maxVisibleLines;    // added in 4.0 to support FldSetMaxVisibleLines
*/

#define CHAR_PASSWORD '*'

typedef struct {
  FieldType *activeField;
} fld_module_t;

extern thread_key_t *fld_key;

static void nop(Err err) {
}

int FldInitModule(void) {
  fld_module_t *module;

  if ((module = xcalloc(1, sizeof(fld_module_t))) == NULL) {
    return -1;
  }

  thread_set(fld_key, module);

  return 0;
}

void *FldReinitModule(void *module) {
  fld_module_t *old = NULL;

  if (module) {
    FldFinishModule();
    thread_set(fld_key, module);
  } else {
    old = (fld_module_t *)thread_get(fld_key);
    FldInitModule();
  }

  return old;
}

int FldFinishModule(void) {
  fld_module_t *module = (fld_module_t *)thread_get(fld_key);

  if (module) {
    xfree(module);
  }

  return 0;
}

FieldType *FldGetActiveField(void) {
  fld_module_t *module = (fld_module_t *)thread_get(fld_key);

  return module->activeField;
}

void FldSetActiveField(FieldType *fldP) {
  fld_module_t *module = (fld_module_t *)thread_get(fld_key);

  InsPtEnable(false);

  if (fldP) {
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldSetActiveField field %d", fldP->id);
  } else {
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldSetActiveField field NULL");
  }

  if (module->activeField) {
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldSetActiveField release current focus");
    FldReleaseFocus(module->activeField);
  }

  module->activeField = fldP;

  if (module->activeField) {
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldSetActiveField grab new focus");
//debug(1, "XXX", "ZZZ FldGrabFocus");
    FldGrabFocus(module->activeField);
  }
}

static Int16 FldCharsWidth(FieldType *fldP, char *chars, Int16 len) {
  Int16 width = 0;

  if (fldP && chars) {
    if (fldP->password) {
      width = FntCharWidth(CHAR_PASSWORD) * len;
    } else {
      width = FntCharsWidth(chars, len);
    }
  }

  return width;
}

void FldDrawField(FieldType *fldP) {
  RectangleType rect, hrect;
  IndexedColorType fieldBack, fieldLine, fieldText, fieldBackHigh, oldb, oldf, oldt;
  WinDrawOperation prev;
  UInt16 start, len, th, x, y, j, k;
  Int16 nBefore, nHigh;
  FontID old;

  IN;
  if (fldP) {
    fieldBack = UIColorGetTableEntryIndex(UIFieldBackground);
    fieldLine = UIColorGetTableEntryIndex(UIFieldTextLines);
    fieldText = UIColorGetTableEntryIndex(UIFieldText);
    fieldBackHigh = UIColorGetTableEntryIndex(UIFieldTextHighlightBackground);

    old = FntSetFont(fldP->fontID);
    oldb = WinSetBackColor(fieldBack);
    oldt = WinSetTextColor(fieldText);
    oldf = WinSetForeColor(fieldLine);
    th = FntCharHeight();

    MemMove(&rect, &fldP->rect, sizeof(RectangleType));
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldDrawField field %d at %d,%d (editable %d, underlined %d)", fldP->id, rect.topLeft.x, rect.topLeft.y, fldP->attr.editable, fldP->attr.underlined);
    WinEraseRectangle(&rect, 0);
/*
    // debug: paint field background with orange
    WinSetForeColor(0x7C);
    WinPaintRectangle(&fldP->rect, 0);
    WinSetForeColor(fieldLine);
*/

    for (j = 0, y = 0; y+th-1 < rect.extent.y; j++, y += th) {
      if (fldP->attr.editable && fldP->attr.underlined) {
        WinDrawLine(rect.topLeft.x, rect.topLeft.y+y+th-1, rect.topLeft.x+rect.extent.x-1, rect.topLeft.y+y+th-1);
      }
      if (fldP->text && j < fldP->numUsedLines && (fldP->top + j) < fldP->totalLines) {
        start = fldP->lines[fldP->top + j].start;
        len = fldP->lines[fldP->top + j].length;
        if (fldP->selLastPos > fldP->selFirstPos && fldP->selFirstPos <= (start + len) && fldP->selLastPos > start) {
          nBefore = fldP->selFirstPos >= start ? fldP->selFirstPos - start : 0;
          nHigh = fldP->selLastPos >= (start + len) ? len - nBefore : fldP->selLastPos - start - nBefore;
          hrect.topLeft.x = rect.topLeft.x + FldCharsWidth(fldP, &fldP->text[start], nBefore);
          hrect.topLeft.y = rect.topLeft.y+y;
          hrect.extent.x = FldCharsWidth(fldP, &fldP->text[start + nBefore], nHigh);
          hrect.extent.y = th;
          WinSetBackColor(fieldBackHigh);
          WinEraseRectangle(&hrect, 0);
          WinSetBackColor(fieldBack);
        }
        prev = WinSetDrawMode(winOverlay);
        if (fldP->password) {
          x = rect.topLeft.x+1;
          for (k = 0; k < len; k++) {
            WinPaintChar(CHAR_PASSWORD, x, rect.topLeft.y+y);
            x += FntCharWidth(CHAR_PASSWORD);
          }
        } else {
          WinPaintChars(&fldP->text[start], len, rect.topLeft.x+1, rect.topLeft.y+y);
        }
        WinSetDrawMode(prev);
      }
    }

    WinSetBackColor(oldb);
    WinSetForeColor(oldf);
    WinSetTextColor(oldt);
    FntSetFont(old);
    fldP->attr.visible = 1;
  }
  OUTV;
}

void FldEraseField(FieldType *fldP) {
  IndexedColorType formFill, oldb;

  IN;
  if (fldP) {
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldEraseField field %d", fldP->id);
    if (fldP->attr.visible) {
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldEraseField field %d is visible", fldP->id);
      formFill = UIColorGetTableEntryIndex(UIFormFill);
      oldb = WinSetBackColor(formFill);
      WinEraseRectangle(&fldP->rect, 0);
      WinSetBackColor(oldb);
      fldP->attr.visible = 0;
    }
  }
  OUTV;
}

/*
Release the handle-based memory allocated to the field’s text and
the associated word-wrapping information.
*/
void FldFreeMemory(FieldType *fldP) {
  IN;
  if (fldP) {
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldFreeMemory field %d", fldP->id);
    if (fldP->lines) {
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldFreeMemory free lines %p", fldP->lines);
      xfree(fldP->lines);
      fldP->lines = NULL;
    }
    if (fldP->textHandle) {
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldFreeMemory free text handle");
      MemHandleFree(fldP->textHandle);
      fldP->textHandle = NULL;
    }
    if (fldP->textBuf) {
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldFreeMemory free text buf");
      pumpkin_heap_free(fldP->textBuf, "FieldTextBuf");
      fldP->textBuf = NULL;
    }
    fldP->text = NULL;
    fldP->textLen = 0;
    fldP->size = 0;
    fldP->textBlockSize = 0;
    fldP->numUsedLines = 0;
    fldP->totalLines = 0;
  }
  OUTV;
}

void FldGetBounds(const FieldType *fldP, RectanglePtr rect) {
  IN;
  if (fldP) {
    MemMove(rect, &fldP->rect, sizeof(RectangleType));
  }
  OUTV;
}

FontID FldGetFont(const FieldType *fldP) {
  FontID id;
  IN;
  id = fldP ? fldP->fontID : 0;
  OUTV;
  return id;
}

void FldGetSelection(const FieldType *fldP, UInt16 *startPosition, UInt16 *endPosition) {
  IN;
  if (fldP) {
    if (startPosition) *startPosition = fldP->selFirstPos;
    if (endPosition) *endPosition = fldP->selLastPos;
  }
  OUTV;
}

static void FldUpdateHandle(FieldType *fldP) {
  UInt16 oldSize, textLen, restSize;
  UInt8 *p, *buf;

  if (fldP) {
    if (fldP->textHandle == NULL) {
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldUpdateHandle textHandle is null");
      if ((fldP->textHandle = MemHandleNew(fldP->textLen + 1)) != NULL) {
        fldP->textBlockSize = fldP->textLen + 1;
        fldP->size = fldP->textBlockSize;
        if ((p = MemHandleLock(fldP->textHandle)) != NULL) {
          DmWrite(p, 0, fldP->text, fldP->size);
          MemHandleUnlock(fldP->textHandle);
        }
        debug(DEBUG_TRACE, PALMOS_MODULE, "FldUpdateHandle alloc textHandle %p blockSize %d", fldP->textHandle, fldP->textBlockSize);
      }
    } else {
      oldSize = fldP->size;
      restSize = fldP->textBlockSize - (fldP->offset + oldSize);
      textLen = fldP->textLen + 1;
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldUpdateHandle textHandle %p blockSize %u offset %u size %u (%u) restSize %u",
        fldP->textHandle, fldP->textBlockSize, fldP->offset, fldP->size, textLen, restSize);
      if (textLen > fldP->size) fldP->size = textLen;

      if ((buf = pumpkin_heap_alloc(fldP->offset + fldP->size + restSize, "FieldUpdateBuf")) != NULL) {
        if ((p = MemHandleLock(fldP->textHandle)) != NULL) {
          if (fldP->offset) MemMove(buf, p, fldP->offset);
          MemMove(buf + fldP->offset, fldP->text, textLen);
          if (restSize) MemMove(buf + fldP->offset + fldP->size, p + fldP->offset + oldSize, restSize);
          MemHandleUnlock(fldP->textHandle);
          if (MemHandleResize(fldP->textHandle, fldP->offset + fldP->size + restSize) == errNone) {
            fldP->textBlockSize = fldP->offset + fldP->size + restSize;
            debug(DEBUG_TRACE, PALMOS_MODULE, "FldUpdateHandle resize textHandle %p blockSize %u offset %u size %u (%u) restSize %u",
              fldP->textHandle, fldP->textBlockSize, fldP->offset, fldP->size, textLen, restSize);
            if ((p = MemHandleLock(fldP->textHandle)) != NULL) {
              DmWrite(p, 0, buf, fldP->textBlockSize);
              MemHandleUnlock(fldP->textHandle);
            }
          }
          pumpkin_heap_free(buf, "FieldUpdateBuf");
        }
      }
    }
  }
}

/*
The handle returned by this function is not necessarily the handle to
the start of the string. If you’ve used FldSetText() to set the
field’s text to a string that is part of a database record, the text
handle points to the start of that record. You’ll need to compute the
offset from the start of the record to the start of the string. You can
either store the offset that you passed to FldSetText or you can
compute the offset by performing pointer arithmetic on the pointer
you get by locking this handle and the pointer returned by
FldGetTextPtr().
If you are obtaining the text handle so that you can edit the field’s
text, you must remove the handle from the field before you do so. If
you change the text while it is being used by a field, the field’s
internal structures specifying the text length, allocated size, and
word wrapping information can become out of sync. To avoid this
problem, remove the text handle from the field, change the text, and
then set the field’s text handle again.
*/
MemHandle FldGetTextHandle(const FieldType *_fldP) {
  FieldType *fldP;
  MemHandle h = NULL;

  IN;
  fldP = (FieldType *)_fldP;
  if (fldP) {
    h = fldP->textHandle;
  }
  OUTV;

  return h;
}

Char *FldGetTextPtr(const FieldType *fldP) {
  Char *p;
  IN;
  p = fldP ? fldP->text : NULL;
  OUTV;
  return p;
}

Boolean FldHandleEvent(FieldType *fldP, EventType *eventP) {
  EventType event;
  FontID old;
  UInt16 line, offset;
  Char c;
  Boolean handled = false;

  IN;
  if (fldP && eventP) {
    switch (eventP->eType) {
      case keyDownEvent:
//debug(1, "XXX", "ZZZ keyDownEvent usable=%d editable=%d focus=%d", fldP->attr.usable, fldP->attr.editable, fldP->attr.hasFocus);
        if (fldP->attr.usable && fldP->attr.editable && fldP->attr.hasFocus && !(eventP->data.keyDown.modifiers & commandKeyMask)) {
          switch (eventP->data.keyDown.chr) {
            case '\b':
              offset = FldGetInsPtPosition(fldP);
              if (offset > 0) {
                FldDelete(fldP, offset-1, offset);
              }
              break;
            case '\r':
              debug(DEBUG_TRACE, PALMOS_MODULE, "FldHandleEvent CR");
              break;
            case '\n':
              debug(DEBUG_TRACE, PALMOS_MODULE, "FldHandleEvent NL");
              if (!fldP->attr.singleLine) {
                c = eventP->data.keyDown.chr;
                FldInsert(fldP, &c, 1);
              }
              break;
            default:
              if (eventP->data.keyDown.chr >= 32) {
                c = eventP->data.keyDown.chr;
                debug(DEBUG_TRACE, PALMOS_MODULE, "FldHandleEvent key %d", c);
//debug(1, "XXX", "ZZZ FldInsert %c", c);
                FldInsert(fldP, &c, 1);
              }
              break;
          }
          InsPtEnable(fldP->textLen == 0 || fldP->insPtYPos < fldP->numUsedLines);
          handled = true;
        } else {
        }
        break;

      case penDownEvent:
        if (fldP->attr.usable && fldP->attr.editable) {
          if (RctPtInRectangle(eventP->screenX, eventP->screenY, &fldP->rect)) {
            debug(DEBUG_TRACE, PALMOS_MODULE, "FldHandleEvent penDown inside field %d", fldP->id);
            if (fldP->selFirstPos < fldP->selLastPos) {
              FldSetSelection(fldP, 0, 0);
            }
            MemSet(&event, sizeof(EventType), 0);
            event.eType = fldEnterEvent;
            event.screenX = eventP->screenX;
            event.screenY = eventP->screenY;
            event.data.fldEnter.fieldID = fldP->id;
            event.data.fldEnter.pField = fldP;
            EvtAddEventToQueue(&event);
            handled = true;
          }
        }
        break;

      case penUpEvent:
        if (RctPtInRectangle(eventP->screenX, eventP->screenY, &fldP->rect)) {
          debug(DEBUG_TRACE, PALMOS_MODULE, "FldHandleEvent penUp inside field %d", fldP->id);
        }
        break;

      case fldEnterEvent:
//debug(1, "XXX", "ZZZ fldEnterEvent");
        debug(DEBUG_TRACE, PALMOS_MODULE, "FldHandleEvent fldEnter field %d", fldP->id);
        FldSetActiveField(NULL);
        old = FntSetFont(fldP->fontID);
        line = (eventP->screenY - fldP->rect.topLeft.y) / FntCharHeight();
        if (line < fldP->numUsedLines && (fldP->top + line) < fldP->totalLines) {
          fldP->insPtYPos = line;
          fldP->insPtXPos = FntWidthToOffset(&fldP->text[fldP->lines[fldP->top + line].start], fldP->lines[fldP->top + line].length, eventP->screenX - fldP->rect.topLeft.x, NULL, NULL);
//debug(1, "XXX", "ZZZ x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
        }
        FntSetFont(old);
//debug(1, "XXX", "ZZZ FldSetActiveField");
        FldSetActiveField(fldP);
        break;

      default:
        break;
    }
  }
  OUTV;

  return handled;
}

void FldCut(FieldType *fldP) {
  IN;
  if (fldP) {
    if (fldP->attr.editable) {
      FldCopy(fldP);
      FldDelete(fldP, fldP->selFirstPos, fldP->selLastPos);
      fldP->selLastPos = fldP->selFirstPos;
      FldDrawField(fldP);
    }
  }
  OUTV;
}

void FldCopy(const FieldType *fldP) {
  Int16 len;

  IN;
  if (fldP) {
    if (fldP->text && fldP->textLen) {
      len = fldP->selLastPos - fldP->selFirstPos;
      if (len > 0) {
        ClipboardAddItem(clipboardText, &fldP->text[fldP->selFirstPos], len);
      }
    }
  }
  OUTV;
}

void FldPaste(FieldType *fldP) {
  UInt16 length;
  MemHandle h;
  char *s;

  IN;
  if (fldP) {
    if ((h = ClipboardGetItem(clipboardText, &length)) != NULL) {
      if (length > 0 && (s = MemHandleLock(h)) != NULL) {
        FldInsert(fldP, s, length);
        MemHandleUnlock(h);
      }
    }
  }
  OUTV;
}

static void FldCalcLineInfo(FieldType *fldP) {
  UInt16 drawnLines, totalLines;

  IN;
  if (fldP) {
    if (fldP->lines) {
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldCalcLineInfo freeing lines %p", fldP->lines);
      xfree(fldP->lines);
      fldP->lines = NULL;
    }
    fldP->numUsedLines = 0;
    fldP->totalLines = 0;

    if (fldP->text && fldP->textLen) {
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldCalcLineInfo text %p len %d", fldP->text, fldP->textLen);
      fldP->totalLines = 1000;
      fldP->lines = xcalloc(fldP->totalLines, sizeof(LineInfoType));
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldCalcLineInfo alloc lines %p", fldP->lines);
      WinDrawCharBox(fldP->text, fldP->textLen, fldP->fontID, &fldP->rect, false, &drawnLines, &totalLines, NULL, fldP->lines);
      fldP->numUsedLines = drawnLines;
      fldP->totalLines = totalLines;
      fldP->lines = xrealloc(fldP->lines, fldP->totalLines * sizeof(LineInfoType));
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldCalcLineInfo realloc lines %p", fldP->lines);
      FldSetInsertionPoint(fldP, FldGetInsPtPosition(fldP));
    }
  }
  OUTV;
}

void FldRecalculateField(FieldType *fldP, Boolean redraw) {
  IN;
  FldCalcLineInfo(fldP);
  if (fldP->attr.visible && redraw) {
    FldDrawField(fldP);
  }
  OUTV;
}

void FldSetBounds(FieldType *fldP, const RectangleType *rP) {
  IN;
  if (fldP && rP) {
    FldEraseField(fldP);
    MemMove(&fldP->rect, rP, sizeof(RectangleType));
    FldCalcLineInfo(fldP);
    if (fldP->attr.visible) {
      FldDrawField(fldP);
    }
  }
  OUTV;
}

void FldSetFont(FieldType *fldP, FontID fontID) {
  IN;
  if (fldP) {
    if (fldP->attr.visible) {
      FldEraseField(fldP);
    }
    fldP->fontID = fontID;
    FldCalcLineInfo(fldP);
    if (fldP->attr.visible) {
      FldDrawField(fldP);
    }
  }
  OUTV;
}

/*
The handle that you pass to this function is assumed to contain a
null-terminated string starting at offset bytes in the memory
chunk. The string should be between 0 and size - 1 bytes in length.
The field does not make a copy of the memory chunk or the string
data; instead, it stores the handle to the record in its structure.
FldSetText updates the word-wrapping information and places
the insertion point after the last visible character.
Because FldSetText (and FldSetTextHandle) may be used to
edit database records, they do not free the memory associated with
the previous text handle.
*/
void FldSetText(FieldType *fldP, MemHandle textHandle, UInt16 offset, UInt16 size) {
  Char *s;
  UInt16 len;
  UInt32 handleSize;

  IN;
  if (fldP) {
    debug(DEBUG_TRACE, "Field", "FldSetText fld %p handle %p offset %d size %d", fldP, textHandle, offset, size);
    fldP->textHandle = NULL;
    fldP->offset = 0;
    fldP->size = 0;
    fldP->textBlockSize = 0;
    handleSize = textHandle ? MemHandleSize(textHandle) : 0;

    if (fldP->textBuf == NULL) {
      fldP->maxChars = handleSize;
      fldP->textBuf = pumpkin_heap_alloc(fldP->maxChars, "FieldTextBuf");
      debug(DEBUG_TRACE, "Field", "FldSetText alloc textBuf=%p size=%d", fldP->textBuf, fldP->maxChars);
      fldP->text = fldP->textBuf;
      fldP->textLen = 0;
    } else if (fldP->maxChars < handleSize) {
      debug(DEBUG_TRACE, "Field", "FldSetText maxChars %d < handleSize %d", fldP->maxChars, handleSize);
      fldP->maxChars = handleSize;
      debug(DEBUG_TRACE, "Field", "FldSetText realloc before textBuf=%p", fldP->textBuf);
      fldP->textBuf = pumpkin_heap_realloc(fldP->textBuf, fldP->maxChars, "FieldTextBuf");
      debug(DEBUG_TRACE, "Field", "FldSetText realloc after  textBuf=%p", fldP->textBuf);
      fldP->text = fldP->textBuf;
      fldP->textLen = 0;
    }

    if (textHandle) {
      debug(DEBUG_TRACE, "Field", "FldSetText textHandle not NULL");
      fldP->textHandle = textHandle;
      fldP->textBlockSize = handleSize;
      if (size > fldP->textBlockSize) {
        debug(DEBUG_TRACE, "Field", "FldSetText size %d > textBlockSize %d", size, fldP->textBlockSize);
        size = fldP->textBlockSize;
      }
      if (offset > fldP->textBlockSize) {
        debug(DEBUG_TRACE, "Field", "FldSetText offset %d > textBlockSize %d", offset, fldP->textBlockSize);
        offset = 0;
        size = 0;
      }
      if ((offset + size) > fldP->textBlockSize) {
        debug(DEBUG_TRACE, "Field", "FldSetText offset+size %d > textBlockSize %d", offset + size, fldP->textBlockSize);
        size = fldP->textBlockSize - offset;
      }
      fldP->offset = offset;
      fldP->size = size;
      debug(DEBUG_TRACE, "Field", "FldSetText offset=%d size=%d", fldP->offset, fldP->size);
      if ((s = MemHandleLock(textHandle)) != NULL) {
        if (fldP->text) {
          len = fldP->size;
          debug(DEBUG_TRACE, "Field", "FldSetText len=%d", len);
          if (len > fldP->maxChars) {
            len = fldP->maxChars;
            debug(DEBUG_TRACE, "Field", "FldSetText len %d > maxChars %d, len=%d", len, fldP->maxChars, len);
          }
          MemMove(fldP->text, &s[offset], len);
          fldP->textLen = StrLen(&s[offset]);
          debug(DEBUG_TRACE, "Field", "FldSetText MemMove(%p, %p, %d)", fldP->text, &s[offset], len);
          debug(DEBUG_TRACE, "Field", "FldSetText texLen=%d", fldP->textLen);
        }
        MemHandleUnlock(textHandle);
      }
    }
    FldCalcLineInfo(fldP);
  }
  OUTV;
}

void FldSetTextHandle(FieldType *fldP, MemHandle textHandle) {
  UInt16 size;

  IN;
  debug(DEBUG_TRACE, "Field", "FldSetTextHandle handle %p", textHandle);
  size = textHandle ? MemHandleSize(textHandle) : 0;
  FldSetText(fldP, textHandle, 0, size);
  OUTV;
}

/*
The field never frees the string that you pass to this function, even
when the field itself is freed. You must free the string yourself.
Before you free the string, make sure the field is not still displaying
it. Set the field’s string pointer to some other string or call
FldSetTextPtr(fldP, NULL) before freeing a string you have
passed using this function.
*/
void FldSetTextPtr(FieldType *fldP, Char *textP) {
  IN;
  if (fldP) {
    debug(DEBUG_TRACE, "Field", "FldSetTextPtr %p", textP);
    if (!fldP->attr.editable) {
      if (textP) {
        fldP->text = textP;
        fldP->textLen = StrLen(textP);
      } else {
        fldP->text = NULL;
        fldP->textLen = 0;
      }
      FldCalcLineInfo(fldP);
    }
  }
  OUTV;
}

void FldSetUsable(FieldType *fldP, Boolean usable) {
  IN;
  if (fldP) {
    fldP->attr.usable = usable;
  }
  OUTV;
}

void FldSetSelection(FieldType *fldP, UInt16 startPosition, UInt16 endPosition) {
  IN;
  if (fldP) {
    if (startPosition < fldP->textLen && endPosition <= fldP->textLen && startPosition <= endPosition) {
      fldP->selFirstPos = startPosition;
      fldP->selLastPos = endPosition;
      if (fldP->attr.visible) {
        FldDrawField(fldP);
      }
    }
  }
  OUTV;
}

static void FldGrabFocusEx(FieldType *fldP, Boolean grab) {
  FontID old;
  Int16 x, y;

  IN;
  if (fldP) {
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldGrabFocus field %d (visible %d, editable %d)", fldP->id, fldP->attr.visible, fldP->attr.editable);
    if (grab) fldP->attr.hasFocus = true;
//debug(1, "XXX", "ZZZ FldGrabFocusEx grab=%d focus=%d visible=%d editable=%d", grab, fldP->attr.hasFocus, fldP->attr.visible, fldP->attr.editable);

    if (fldP->attr.visible && fldP->attr.editable) {
      x = fldP->rect.topLeft.x;
      y = fldP->rect.topLeft.y+1;
      old = FntSetFont(fldP->fontID);

//debug(1, "XXX", "ZZZ grab y=%d used=%d top=%d total=%d", fldP->insPtYPos, fldP->numUsedLines, fldP->top, fldP->totalLines);
      if (fldP->text && fldP->insPtYPos < fldP->numUsedLines && (fldP->top + fldP->insPtYPos) < fldP->totalLines) {
        x += FldCharsWidth(fldP, &fldP->text[fldP->lines[fldP->top + fldP->insPtYPos].start], fldP->insPtXPos);
      }
      y += fldP->insPtYPos * FntCharHeight();
//debug(1, "XXX", "ZZZ grab now y=%d", fldP->insPtYPos);
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldGrabFocus insPtEnable 1");
      InsPtSetHeight(FntCharHeight()-2);
//debug(1, "XXX", "ZZZ InsPtSetLocation %d,%d", x, y);
      InsPtSetLocation(x, y);
      if (grab) InsPtEnable(true);
      FntSetFont(old);
    } else {
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldGrabFocus insPtEnable 0");
      if (grab) InsPtEnable(false);
    }
  }
  OUTV;
}

// Turn the insertion point on (if the specified field is visible) and position the blinking insertion point in the field.
// You rarely need to call this function directly. Instead, use FrmSetFocus, which calls FldGrabFocus for you.
// One instance where you need to call FldGrabFocus directly is to programmatically set the focus in a field that is contained in a table cell.
// This function sets the field attribute hasFocus to true.

void FldGrabFocus(FieldType *fldP) {
  FldGrabFocusEx(fldP, true);
}

// Turn the blinking insertion point off if the field is visible and has the current focus, reset the Graffiti state, and reset the undo state.
// This function sets the field attribute hasFocus to false. (See FieldAttrType.)
// Usually, you don’t need to call this function. If the field is in a form or in a table that doesn’t use custom drawing functions, the field
// code releases the focus for you when the focus changes to some other control. If your field is in any other type of object, such as a
// table that uses custom drawing functions or a gadget, you must call FldReleaseFocus when the focus moves away from the field.

void FldReleaseFocus(FieldType *fldP) {
  IN;
  if (fldP) {
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldReleaseFocus field %d (visible %d, editable %d)", fldP->id, fldP->attr.visible, fldP->attr.editable);
    if (fldP->attr.visible && fldP->attr.editable) {
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldReleaseFocus insPtEnable 0");
      InsPtEnable(false);
    }
    fldP->attr.hasFocus = false;
//debug(1, "XXX", "FldReleaseFocus focus=%d", fldP->attr.hasFocus);
  }
  OUTV;
}

UInt16 FldGetInsPtPosition(const FieldType *fldP) {
  UInt16 i, offset = 0;

  if (fldP) {
    for (i = 0; i < fldP->insPtYPos && i < fldP->numUsedLines && i < fldP->totalLines; i++) {
      offset += fldP->lines[i].length;
    }
    offset += fldP->insPtXPos;
  }

  return offset;
}

void FldSetInsPtPosition(FieldType *fldP, UInt16 pos) {
  // Set the location of the insertion point for a given string position.
  // If the position is beyond the visible text, the field is scrolled until the position is visible.

  FldSetInsertionPoint(fldP, pos);
  FldSetScrollPosition(fldP, pos);
}

void FldSetInsertionPoint(FieldType *fldP, UInt16 pos) {
  UInt16 i, offset;

  // Set the location of the insertion point based on a specified string position.
  // This routine differs from FldSetInsPtPosition in that it doesn’t make the character position visible.
  // FldSetInsertionPoint also doesn’t make the field the current focus of input if it was not already.
  // If pos indicates a position beyond the end of the text in the field, the insertion point is set to the end of the field’s text.

//debug(1, "XXX", "FldSetInsertionPoint pos=%d", pos);
  if (fldP) {
//debug(1, "XXX", "FldSetInsertionPoint totalLines=%d", fldP->totalLines);
    if (fldP->totalLines > 0) {
      for (i = 0, offset = 0; i < fldP->totalLines; i++) {
        if (offset + fldP->lines[i].length >= pos) {
          fldP->insPtYPos = i;
          fldP->insPtXPos = pos - offset;
          break;
        }
        offset += fldP->lines[i].length;
      }
//debug(1, "XXX", "FldSetInsertionPoint end loop i=%d", i);
      if (i == fldP->totalLines) {
        fldP->insPtYPos = i-1;
        fldP->insPtXPos = fldP->lines[i-1].length;
//debug(1, "XXX", "FldSetInsertionPoint x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
      }
    } else {
      fldP->insPtYPos = 0;
      fldP->insPtXPos = 0;
//debug(1, "XXX", "FldSetInsertionPoint else x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
    }
  }
}

UInt16 FldGetScrollPosition(const FieldType *fldP) {
  UInt16 pos = 0;

  IN;
  if (fldP && fldP->lines) {
    pos = fldP->lines[fldP->top].start;
  }
  OUTV;

  return pos;
}

void FldSetScrollPosition(FieldType *fldP, UInt16 pos) {
  Boolean redraw = false;
  UInt16 i;

  IN;
  if (fldP && fldP->totalLines > 0) {
    for (i = 0; i < fldP->totalLines; i++) {
      if (pos >= fldP->lines[i].start && pos < (fldP->lines[i].start + fldP->lines[i].length)) {
        fldP->top = i;
        redraw = true;
        break;
      }
    }
    if (!redraw) {
      fldP->top = fldP->totalLines-1;
      redraw = true;
    }
    if (redraw && fldP->attr.visible) {
      FldDrawField(fldP);
    }
  }
  OUTV;
}

UInt16 FldGetTextLength(const FieldType *fldP) {
  UInt16 len;
  IN;
  len = fldP ? fldP->textLen : 0;
  OUTV;
  return len;
}

void FldScrollField(FieldType *fldP, UInt16 linesToScroll, WinDirectionType direction) {
  UInt16 top;

  IN;
  if (fldP) {
    switch (direction) {
      case winUp:
        top = (fldP->top >= linesToScroll) ? fldP->top - linesToScroll : 0;
        break;
      case winDown:
        top = (fldP->top + linesToScroll) <= fldP->totalLines ? fldP->top + linesToScroll : fldP->totalLines - 1;
        break;
      default:
        top = fldP->top;
        break;
    }

    if (fldP->top != top) {
      fldP->top = top;
      FldDrawField(fldP);
    }
  }
  OUTV;
}

Boolean FldScrollable(const FieldType *fldP, WinDirectionType direction) {
  Boolean scrollable = false;

  IN;
  if (fldP) {
    switch (direction) {
      case winUp:
        scrollable = fldP->top > 0;
        break;
      case winDown:
        scrollable = (fldP->top + fldP->numUsedLines) < fldP->totalLines;
        break;
      default:
        break;
    }
  }
  OUTV;

  return scrollable;
}

UInt16 FldGetVisibleLines(const FieldType *fldP) {
  return fldP ? fldP->numUsedLines : 0;
}

UInt16 FldGetTextHeight(const FieldType *fldP) {
  UInt16 height, th;
  FontID old;

  IN;
  height = 0;

  if (fldP && fldP->numUsedLines) {
    old = FntSetFont(fldP->fontID);
    th = FntCharHeight();
    FntSetFont(old);
    height = th * fldP->numUsedLines;
  }
  OUTV;

  return height;
}

UInt16 FldCalcFieldHeight(const Char *chars, UInt16 maxWidth) {
  RectangleType rect;
  UInt16 totalLines = 0;

  IN;
//debug(1, "XXX", "FldCalcFieldHeight char=%p maxWidth=%d", chars, maxWidth);
  if (chars) {
//debug(1, "XXX", "FldCalcFieldHeight chars not null");
    RctSetRectangle(&rect, 0, 0, maxWidth, 32767);
    if (chars[0]) {
//debug(1, "XXX", "FldCalcFieldHeight chars not empty \"%s\" len %d", chars, StrLen(chars));
      WinDrawCharBox((char *)chars, StrLen(chars), FntGetFont(), &rect, false, NULL, &totalLines, NULL, NULL);
//debug(1, "XXX", "FldCalcFieldHeight \"%s\" total=%d", chars, totalLines);
    } else {
      totalLines = 1;
//debug(1, "XXX", "FldCalcFieldHeight empty total=%d", totalLines);
   }
  } else {
    totalLines = 1;
//debug(1, "XXX", "FldCalcFieldHeight null total=%d", totalLines);
  }
  OUTV;

  return totalLines;
}

UInt16 FldWordWrap(const Char *chars, Int16 maxWidth) {
  Int16 n = 0;

  IN;
  if (chars) {
    n = FntWidthToOffset(chars, StrLen(chars), maxWidth, NULL, NULL);
  }
  OUTV;

  return n;
}

void FldCompactText(FieldType *fldP) {
  IN;
  OUTV;
}

// Return true if the field has been modified since the text value was set.
Boolean FldDirty(const FieldType *fldP) {
  Boolean dirty;
  IN;
  dirty = fldP ? fldP->attr.dirty : false;
  OUTV;
  return dirty;
}

void FldSetDirty(FieldType *fldP, Boolean dirty) {
  IN;
  if (fldP) {
    fldP->attr.dirty = dirty;
  }
  OUTV;
}

UInt16 FldGetMaxChars(const FieldType *fldP) {
  UInt16 max;
  IN;
  max = fldP ? fldP->maxChars : 0;
  OUTV;
  return max;
}

void FldSetMaxChars(FieldType *fldP, UInt16 maxChars) {
  UInt16 old;
  char *aux;

  IN;
  if (fldP) {
    if (maxChars > maxFieldTextLen) maxChars = maxFieldTextLen;
    old = fldP->maxChars;
    fldP->maxChars = maxChars;
    if (fldP->textBuf) {
      aux = pumpkin_heap_alloc(fldP->maxChars, "FieldTextBuf");
      xmemcpy(aux, fldP->textBuf, old);
      pumpkin_heap_free(fldP->textBuf, "FieldTextBuf");
      fldP->textBuf = aux;
      fldP->text = fldP->textBuf;
    }
  }
  OUTV;
}

static void FldInsertOneChar(FieldType *fldP, Char c, UInt16 offset) {
  Int16 i;

  IN;
  debug(DEBUG_TRACE, PALMOS_MODULE, "FldInsertOneChar 0x%02X at %d", (UInt8)c, offset);
  if (fldP->textBuf == NULL) {
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldInsertOneChar allocating %d bytes for textBuf", fldP->maxChars);
    fldP->textBuf = pumpkin_heap_alloc(fldP->maxChars, "FieldTextBuf");
    fldP->text = fldP->textBuf;
    fldP->textLen = 0;
  }

  if (fldP->textLen < fldP->maxChars) {
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldInsertOneChar textLen (%d) < maxChars (%d)", fldP->textLen, fldP->maxChars);
    if (offset > fldP->textLen) {
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldInsertOneChar offset (%d) > textLen (%d)", offset, fldP->textLen);
      offset = fldP->textLen;
    }
    if (fldP->textLen) {
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldInsertOneChar textLen (%d) > 0", fldP->textLen);
      for (i = fldP->textLen-1; i >= offset; i--) {
        debug(DEBUG_TRACE, PALMOS_MODULE, "FldInsertOneChar text[%d] (%d) = text[%d] (%d)", i+1, fldP->text[i+1], i, fldP->text[i]);
        fldP->text[i+1] = fldP->text[i];
      }
    }
    fldP->text[offset] = c;
    fldP->textLen++;
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldInsertOneChar text[%d] = 0x%02X, textLen %d", offset, (UInt8)fldP->text[offset], fldP->textLen);
    if (c == '\n') {
      fldP->insPtXPos = 0;
      fldP->insPtYPos++;
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldInsertOneChar insPtXPos 0 insPtYPos %d", fldP->insPtYPos);
    } else {
      fldP->insPtXPos++;
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldInsertOneChar insPtXPos %d", fldP->insPtXPos);
    }
  }
  OUTV;
}

Boolean FldInsert(FieldType *fldP, const Char *insertChars, UInt16 insertLen) {
  UInt16 i, offset;
  Boolean r = false;

  IN;
  debug(DEBUG_TRACE, PALMOS_MODULE, "FldInsert %d chars", insertLen);
  if (fldP) {
    if (fldP->attr.editable && insertChars && insertLen) {
      if (fldP->selLastPos > fldP->selFirstPos) {
        FldDelete(fldP, fldP->selFirstPos, fldP->selLastPos);
        fldP->selLastPos = fldP->selFirstPos;
      }
      for (i = 0, offset = 0; i < fldP->insPtYPos && i < fldP->numUsedLines && (fldP->top + i) < fldP->totalLines; i++) {
        offset += fldP->lines[fldP->top + i].length;
      }
      offset += fldP->insPtXPos;
      for (i = 0; i < insertLen; i++) {
//debug(1, "XXX", "insert0 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
        FldInsertOneChar(fldP, insertChars[i], offset+i);
//debug(1, "XXX", "insert1 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
        FldCalcLineInfo(fldP);
//debug(1, "XXX", "insert2 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
        FldGrabFocusEx(fldP, false);
//debug(1, "XXX", "insert3 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
      }
      FldUpdateHandle(fldP);
    }

    FldSetDirty(fldP, true);
    if (fldP->attr.visible) {
//debug(1, "XXX", "insert4 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
      FldDrawField(fldP);
//debug(1, "XXX", "insert5 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
    }
  }
  OUTV;

  return r;
}

static void FldDeleteOneChar(FieldType *fldP, UInt16 offset) {
  UInt16 i;

  IN;
  debug(DEBUG_TRACE, PALMOS_MODULE, "FldDeleteOneChar at %d", offset);
  for (i = offset; i < fldP->textLen-1; i++) {
    debug(DEBUG_TRACE, PALMOS_MODULE, "FldDeleteOneChar text[%d] = text[%d] (%d)", i, i+1, fldP->text[i+1]);
    fldP->text[i] = fldP->text[i+1];
  }
  fldP->text[i] = 0;
  fldP->textLen--;
  if (fldP->insPtXPos) {
    fldP->insPtXPos--;
  } else if (fldP->insPtYPos) {
    fldP->insPtYPos--;
    fldP->insPtXPos = fldP->lines[fldP->top + fldP->insPtYPos].length - 1;
  }
  debug(DEBUG_TRACE, PALMOS_MODULE, "FldDeleteOneChar text[%d] = 0, textLen %d", i, fldP->textLen);
  OUTV;
}

/*
end param: if you pass a value that is greater than the number of bytes in
the field, all characters in the field are deleted.
*/
void FldDelete(FieldType *fldP, UInt16 start, UInt16 end) {
  UInt16 i, len;

  IN;
  debug(DEBUG_TRACE, PALMOS_MODULE, "FldDelete %d to %d", start, end);
//debug(1, "XXX", "delete0 x=%d y=%d start=%d end=%d", fldP->insPtXPos, fldP->insPtYPos, start, end);
  if (fldP && fldP->attr.editable && fldP->textLen > 0 && start < end && start < fldP->textLen) {
//debug(1, "XXX", "delete1 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
    if (start == 0 && end == fldP->textLen) {
//debug(1, "XXX", "delete2 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldDelete all");
      for (i = 0; i < fldP->textLen; i++) {
        fldP->text[i] = 0;
      }
      fldP->textLen = 0;
      fldP->insPtXPos = 0;
      FldCalcLineInfo(fldP);
      FldGrabFocusEx(fldP, false);
    } else {
      if (end > fldP->textLen) end = fldP->textLen;
      len = end - start;
      debug(DEBUG_TRACE, PALMOS_MODULE, "FldDelete %d chars", len);
      for (i = 0; i < len; i++) {
//debug(1, "XXX", "delete3 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
        FldDeleteOneChar(fldP, start + i);
//debug(1, "XXX", "delete4 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
        FldCalcLineInfo(fldP);
//debug(1, "XXX", "delete5 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
        FldGrabFocusEx(fldP, false);
//debug(1, "XXX", "delete6 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
      }
    }
    FldUpdateHandle(fldP);
    FldSetDirty(fldP, true);
    if (fldP->attr.visible) {
//debug(1, "XXX", "delete7 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
//debug(1, "XXX", "delete8 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
      FldDrawField(fldP);
//debug(1, "XXX", "delete9 x=%d y=%d", fldP->insPtXPos, fldP->insPtYPos);
    }
  }
  OUTV;
}

// Undo the last change made to the field object, if any.
// Changes include typing, backspaces, delete, paste, and cut.

void FldUndo(FieldType *fldP) {
  NOTIMPLV;
}

UInt16 FldGetTextAllocatedSize(const FieldType *fldP) {
  UInt16 size = 0;

  IN;
  if (fldP) {
    size = fldP->textBlockSize;
  }
  OUTV;

  return size;
}

void FldSetTextAllocatedSize(FieldType *fldP, UInt16 allocatedSize) {
  IN;
  if (fldP) {
    fldP->textBlockSize = allocatedSize;
  }
  OUTV;
}

void FldGetAttributes(const FieldType *fldP, FieldAttrPtr attrP) {
  IN;
  if (fldP) {
    *attrP = fldP->attr;
  }
  OUTV;
}

void FldSetAttributes(FieldType *fldP, const FieldAttrType *attrP) {
  Boolean redraw;

  IN;
  if (fldP && attrP) {
    redraw =  fldP->attr.editable != attrP->editable;
    fldP->attr = *attrP;
    if (redraw && fldP->attr.visible) {
      FldDrawField(fldP);
    }
  }
  OUTV;
}

void FldSendChangeNotification(const FieldType *fldP) {
/*
  EventType event;

  IN;
  if (fldP) {
    xmemset(&event, 0, sizeof(EventType));
    event.eType = fldChangedEvent;
    event.screenX = fldP->rect.topLeft.x;
    event.screenY = fldP->rect.topLeft.y;
    event.data.fldChanged.fieldID = fldP->id;
    event.data.fldChanged.pField = (FieldType *)fldP;
    EvtAddEventToQueue(&event);
  }
  OUTV;
*/
}

// Send a fldHeightChangedEvent to the event queue.
// This function is used internally by the field code. You normally never call it in application code.

void FldSendHeightChangeNotification(const FieldType *fldP, UInt16 pos, Int16 numLines) {
  NOTIMPLV;
}

// Generates an event to cause a dynamically resizable field to expand its height to make its text fully visible.

Boolean FldMakeFullyVisible(FieldType *fldP) {
  NOTIMPLI;
}

UInt16 FldGetNumberOfBlankLines(const FieldType *fldP) {
  UInt16 n = 0;
  FontID old;

  IN;
  if (fldP) {
    old = FntSetFont(fldP->fontID);
    n = fldP->rect.extent.y / FntCharHeight();
    FntSetFont(old);

    if (fldP->numUsedLines) {
      n -= fldP->numUsedLines;
    }
  }
  OUTV;

  return n;
}

void FldSetMaxVisibleLines(FieldType *fldP, UInt8 maxLines) {
  IN;
  if (fldP) {
    fldP->maxVisibleLines = maxLines;
  }
  OUTV;
}

void FldGetScrollValues(const FieldType *fldP, UInt16 *scrollPosP, UInt16 *textHeightP, UInt16 *fieldHeightP) {
  UInt16 scrollPos = 0, textHeight = 0, fieldHeight = 0, th;
  FontID old;

  if (fldP) {
    old = FntSetFont(fldP->fontID);
    th = FntCharHeight();
    FntSetFont(old);

    // the line of text that is the topmost visible line
    scrollPos = fldP->top;

    // the number of lines needed to display the field’s text, given the width of the field
    textHeight = fldP->totalLines;

    // the number of visible lines in the field
    fieldHeight = fldP->rect.extent.y / th;

    debug(DEBUG_TRACE, "Field", "FldGetScrollValues pos=%d th=%d textHeight=%d fieldHeight=%d", scrollPos, th, textHeight, fieldHeight);
  }

  if (scrollPosP) *scrollPosP = scrollPos;
  if (textHeightP) *textHeightP = textHeight;
  if (fieldHeightP) *fieldHeightP = fieldHeight;
}

FieldType *FldNewField(void **formPP, UInt16 id,
  Coord x, Coord y, Coord width, Coord height,
  FontID font, UInt32 maxChars, Boolean editable, Boolean underlined,
  Boolean singleLine, Boolean dynamicSize, JustificationType justification,
  Boolean autoShift, Boolean hasScrollBar, Boolean numeric) {

  FieldType *fldP;
  FormType *formP;

  if ((fldP = pumpkin_heap_alloc(sizeof(FieldType), "Field")) != NULL) {
    fldP->id = id;
    fldP->rect.topLeft.x = x;
    fldP->rect.topLeft.y = y;
    fldP->rect.extent.x = width;
    fldP->rect.extent.y = height;
    fldP->attr.usable = true;
    fldP->attr.editable = editable;
    fldP->attr.underlined = underlined;
    fldP->attr.singleLine = singleLine;
    fldP->attr.dynamicSize = dynamicSize;
    fldP->attr.justification = justification;
    fldP->attr.autoShift = autoShift;
    fldP->attr.hasScrollBar = hasScrollBar;
    fldP->attr.numeric = numeric;
    fldP->maxChars = maxChars;
    fldP->fontID = font;

    if (formPP) {
      formP = *formPP;
      if (formP) {
        fldP->objIndex = formP->numObjects++;
        formP->objects = xrealloc(formP->objects, formP->numObjects * sizeof(FormObjListType));
        if (formP->objects) {
          formP->objects[fldP->objIndex].objectType = frmFieldObj;
          formP->objects[fldP->objIndex].id = id;
          formP->objects[fldP->objIndex].object.field = fldP;
          formP->objects[fldP->objIndex].object.field->formP = formP;
        }
      }
    }
  }

  return fldP;
}

void FldSetPassword(FieldType *fldP, Boolean password) {
  IN;
  if (fldP) {
    fldP->password = password;
  }
  OUTV;
}

void FldReplaceText(FieldType *fldP, char *s, Boolean focus) {
  UInt16 len;
  FieldAttrType attr;
  Boolean old;

  IN;
  if (fldP) {
    FldGetAttributes(fldP, &attr);
    old = attr.editable;
    attr.editable = true;
    FldSetAttributes(fldP, &attr);

    len = FldGetTextLength(fldP);
    if (len) FldDelete(fldP, 0, len);
    FldInsert(fldP, s, StrLen(s));

    attr.editable = old;
    FldSetAttributes(fldP, &attr);

    if (focus) fldP->attr.hasFocus = true;
  }
  OUTV;
}
