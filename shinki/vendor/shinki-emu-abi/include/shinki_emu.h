#ifndef SHINKI_EMU_ABI_H
#define SHINKI_EMU_ABI_H

/* Generated from shinki-emu-abi Rust types by cbindgen 0.29.2. Do not edit. */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#define ShinkiEmuABI_VERSION 3

#define ShinkiEmuMAX_CONFIG_FIELDS 128

#define ShinkiEmuMAX_CONFIG_TEXT 4096

#define ShinkiEmuVALUE_BOOL 1

#define ShinkiEmuVALUE_INTEGER 2

#define ShinkiEmuVALUE_NUMBER 3

#define ShinkiEmuVALUE_STRING 4

#define ShinkiEmuVALUE_ENUM 5

#define ShinkiEmuMAX_CONFIG_DESCRIPTION 512

#define ShinkiEmuMAX_CHOICE_LABEL 64

#define ShinkiEmuMAX_CONFIG_UNIT 16

#define ShinkiEmuCONFIG_FLAG_FILE_PATH 1

#define ShinkiEmuCONFIG_FLAG_DIRECTORY_PATH 2

#define ShinkiEmuCONFIG_FLAG_ADVANCED 4

#define ShinkiEmuCONFIG_FLAG_GAME_RELATIVE 8

#define ShinkiEmuCAP_CPU_SURFACE 1

#define ShinkiEmuCAP_TEXT_HOOK 4

#define ShinkiEmuCAP_LIVE_CONFIGURATION 8

#define ShinkiEmuCAP_RENDER_EXTENT 16

#define ShinkiEmuMAX_DISPLAY_NAME_BYTES 64

#define ShinkiEmuMAX_RENDER_EXTENT 16384

#define ShinkiEmuRENDER_PRESERVE_ASPECT 1

#define ShinkiEmuINPUT_KEY 1

#define ShinkiEmuINPUT_POINTER_MOVE 2

#define ShinkiEmuINPUT_POINTER_BUTTON 3

#define ShinkiEmuINPUT_WHEEL 4

#define ShinkiEmuINPUT_WINDOW 5

#define ShinkiEmuBUTTON_PRIMARY 1

#define ShinkiEmuBUTTON_SECONDARY 2

#define ShinkiEmuBUTTON_MIDDLE 3

#define ShinkiEmuCONTROL_PAUSE 1

#define ShinkiEmuCONTROL_RESUME 2

#define ShinkiEmuCONTROL_RETRY 3

#define ShinkiEmuSESSION_RUNNING 1

#define ShinkiEmuSESSION_PAUSED 2

#define ShinkiEmuSESSION_WAITING 3

#define ShinkiEmuSESSION_FAILED 4

#define ShinkiEmuSESSION_FINISHED 5

/**
 * Only a retained, retry-safe operation may set this flag in SESSION_FAILED.
 * Panic and fatal native failures leave it clear; Host offers End only.
 */
#define ShinkiEmuSESSION_CAN_RETRY 1

#define ShinkiEmuMODULE_OWNER_VERSION 1

#define ShinkiEmuLOG_ERROR 1

#define ShinkiEmuLOG_WARN 2

#define ShinkiEmuLOG_INFO 3

#define ShinkiEmuLOG_DEBUG 4

#define ShinkiEmuLOG_TRACE 5

#define ShinkiEmuFIELD_TEXT 1

#define ShinkiEmuFIELD_I64 2

#define ShinkiEmuFIELD_U64 3

#define ShinkiEmuFIELD_F64 4

#define ShinkiEmuFIELD_BOOL 5

#define ShinkiEmuTEXT_DIALOGUE 1

#define ShinkiEmuTEXT_SPEAKER 2

#define ShinkiEmuTEXT_CHOICE 3

#define ShinkiEmuWINDOW_TITLE 1

#define ShinkiEmuWINDOW_FULLSCREEN 2

#define ShinkiEmuWINDOW_ICON 3

#define ShinkiEmuWINDOW_POINTER 4

#define ShinkiEmuWINDOW_MODAL 5

#define ShinkiEmuWINDOW_SIZE 6

#define ShinkiEmuWINDOW_POSITION 7

/**
 * flags is exactly 0/1; Host reports actual state using matching result bit.
 */
#define ShinkiEmuWINDOW_VISIBILITY 8

#define ShinkiEmuWINDOW_MINIMIZED 9

/**
 * Read actual window state without mutation. flags must be zero; inspect result valid_fields.
 */
#define ShinkiEmuWINDOW_QUERY_STATE 10

#define ShinkiEmuMODAL_TEXT_INPUT 1

#define ShinkiEmuMAX_MODAL_TEXT 65536

#define ShinkiEmuMAX_MODAL_BUTTONS 16

#define ShinkiEmuMAX_MODAL_BUTTON_LABEL 256

#define ShinkiEmuWINDOW_RESULT_MODAL 1

#define ShinkiEmuWINDOW_RESULT_SIZE 2

#define ShinkiEmuWINDOW_RESULT_POSITION 4

#define ShinkiEmuWINDOW_RESULT_MODE 8

#define ShinkiEmuWINDOW_RESULT_VISIBILITY 16

#define ShinkiEmuWINDOW_RESULT_MINIMIZED 32

#define ShinkiEmuFULLSCREEN_WINDOWED 0

#define ShinkiEmuFULLSCREEN_BORDERLESS 1

#define ShinkiEmuFULLSCREEN_EXCLUSIVE 2

#define ShinkiEmuPOINTER_SET_POSITION 1

#define ShinkiEmuPOINTER_SET_VISIBILITY 2

#define ShinkiEmuPOINTER_VISIBLE 4

#define ShinkiEmuPOINTER_SET_AUTOHIDE 8

#define ShinkiEmuPOINTER_SET_X 16

#define ShinkiEmuPOINTER_SET_Y 32

#define ShinkiEmuSESSION_MENU_VERSION 1

/**
 * Core-defined control commands start here; built-in CONTROL_* stay below.
 */
#define ShinkiEmuCONTROL_CORE_BASE 4096

#define ShinkiEmuSESSION_MENU_MAX_ITEMS 16

#define ShinkiEmuSESSION_MENU_LABEL_MAX 64

#define ShinkiEmuMENU_ICON_GENERIC 0

#define ShinkiEmuMENU_ICON_QUICK_SAVE 1

#define ShinkiEmuMENU_ICON_QUICK_LOAD 2

#define ShinkiEmuMENU_ICON_SAVE 3

#define ShinkiEmuMENU_ICON_LOAD 4

#define ShinkiEmuMENU_ICON_AUTO 5

#define ShinkiEmuMENU_ICON_SKIP 6

#define ShinkiEmuMENU_ICON_BACKLOG 7

#define ShinkiEmuMENU_ICON_SETTINGS 8

#define ShinkiEmuSURFACE_CPU 1

#define ShinkiEmuPIXEL_RGBA8 1

#define ShinkiEmuPIXEL_BGRA8 2

#define ShinkiEmuCOLOR_SRGB 1

#define ShinkiEmuCOLOR_LINEAR_SRGB 2

#define ShinkiEmuALPHA_OPAQUE 1

#define ShinkiEmuALPHA_IGNORED 2

#define ShinkiEmuALPHA_STRAIGHT 3

#define ShinkiEmuALPHA_PREMULTIPLIED 4

#define ShinkiEmuROWS_TOP_DOWN 1

#define ShinkiEmuROWS_BOTTOM_UP 2

#define ShinkiEmuFRAME_PENDING 0

#define ShinkiEmuFRAME_READY 1

#define ShinkiEmuFRAME_UNCHANGED 2

#define ShinkiEmuMAX_FRAME_BYTES ((512 * 1024) * 1024)

typedef int32_t ShinkiEmuStatus;

typedef struct ShinkiEmuHeader {
  uint32_t size;
  uint32_t version;
} ShinkiEmuHeader;

/**
 * Borrowed bytes. Null is permitted only for an empty span. The owner keeps
 * storage immutable and alive for the whole call (descriptors: library life).
 */
typedef struct ShinkiEmuBytes {
  const uint8_t *data;
  size_t len;
} ShinkiEmuBytes;

typedef struct ShinkiEmuLogField {
  struct ShinkiEmuBytes name;
  uint32_t kind;
  uint32_t reserved;
  struct ShinkiEmuBytes text;
  int64_t signed_;
  uint64_t unsigned_;
  double number;
} ShinkiEmuLogField;

typedef struct ShinkiEmuLogRecord {
  uint32_t level;
  uint32_t dropped_fields;
  /**
   * Empty targets identify unstructured native logs; the sink names them.
   */
  struct ShinkiEmuBytes target;
  /**
   * May be empty for native logging systems without structured event names.
   */
  struct ShinkiEmuBytes event;
  struct ShinkiEmuBytes message;
  const struct ShinkiEmuLogField *fields;
  size_t field_count;
} ShinkiEmuLogRecord;

/**
 * Installed before descriptor/probe/open; sink storage lives until process exit.
 * Callback is thread-safe; all spans are borrowed only for this invocation.
 */
typedef struct ShinkiEmuDiagnostics {
  struct ShinkiEmuHeader header;
  void *context;
  void (*emit)(void*, const struct ShinkiEmuLogRecord*);
  void (*flush)(void*);
} ShinkiEmuDiagnostics;

/**
 * Caller-owned error output. Every failure supplies a stable diagnostic code;
 * status remains machine-readable even when the optional error pointer is null.
 */
typedef struct ShinkiEmuError {
  uint32_t code_len;
  uint32_t message_len;
  uint8_t code[128];
  uint8_t message[512];
} ShinkiEmuError;

/**
 * Only the member selected by `kind` is meaningful. Boolean uses integer 0/1.
 * Reserved members must be zero; no Rust enum or owning string crosses the ABI.
 */
typedef struct ShinkiEmuConfigValue {
  uint32_t kind;
  uint32_t reserved;
  int64_t integer;
  double number;
  struct ShinkiEmuBytes text;
} ShinkiEmuConfigValue;

/**
 * Path flags apply only to STRING fields and are mutually exclusive;
 * GAME_RELATIVE requires one of them. `choice_labels` is null when
 * `choice_count` is zero, otherwise exactly `choice_count` user-visible labels
 * parallel to `choices`. `step` is 0 for none, a positive finite value for
 * NUMBER, or an integral value of at least 1 for INTEGER.
 */
typedef struct ShinkiEmuConfigField {
  struct ShinkiEmuBytes id;
  struct ShinkiEmuBytes label;
  struct ShinkiEmuBytes group;
  struct ShinkiEmuConfigValue default_value;
  int64_t integer_min;
  int64_t integer_max;
  double number_min;
  double number_max;
  uint32_t max_bytes;
  uint32_t flags;
  const struct ShinkiEmuBytes *choices;
  size_t choice_count;
  struct ShinkiEmuBytes description;
  const struct ShinkiEmuBytes *choice_labels;
  double step;
  struct ShinkiEmuBytes unit;
} ShinkiEmuConfigField;

typedef struct ShinkiEmuExtent {
  uint32_t width;
  uint32_t height;
} ShinkiEmuExtent;

/**
 * Render extents a core accepts in `OpenRequest.render_extent`. All members
 * are zero unless the descriptor declares `CAP_RENDER_EXTENT`. A zero
 * `default` means the core's native raster size.
 */
typedef struct ShinkiEmuRenderExtentSupport {
  struct ShinkiEmuExtent min;
  struct ShinkiEmuExtent max;
  struct ShinkiEmuExtent default_;
  uint32_t flags;
  uint32_t reserved;
} ShinkiEmuRenderExtentSupport;

/**
 * `display_name` is the emulator name and `family_name` the engine name shown
 * to users; both are 1..=64 UTF-8 bytes without control characters.
 */
typedef struct ShinkiEmuDescriptor {
  struct ShinkiEmuHeader header;
  struct ShinkiEmuBytes family_id;
  struct ShinkiEmuBytes plugin_id;
  struct ShinkiEmuBytes version;
  uint64_t capabilities;
  const struct ShinkiEmuBytes *formats;
  size_t format_count;
  const struct ShinkiEmuConfigField *configuration;
  size_t configuration_count;
  struct ShinkiEmuBytes display_name;
  struct ShinkiEmuBytes family_name;
  struct ShinkiEmuRenderExtentSupport render;
} ShinkiEmuDescriptor;

/**
 * Probe strings borrow module storage until the next ordinary module call.
 * Manager copies bounded metadata before making that call. NO_MATCH is normal.
 */
typedef struct ShinkiEmuProbeResult {
  struct ShinkiEmuBytes game_id;
  struct ShinkiEmuBytes format;
  uint32_t confidence_permyriad;
  uint32_t reserved;
} ShinkiEmuProbeResult;

typedef struct ShinkiEmuConfigEntry {
  struct ShinkiEmuBytes id;
  struct ShinkiEmuConfigValue value;
} ShinkiEmuConfigEntry;

typedef struct ShinkiEmuWindowState {
  uint32_t drawable_width;
  uint32_t drawable_height;
  double scale_factor;
  uint32_t focused;
  uint32_t suspended;
} ShinkiEmuWindowState;

typedef struct ShinkiEmuTextItem {
  uint64_t id;
  uint32_t kind;
  uint32_t reserved;
  struct ShinkiEmuBytes text;
} ShinkiEmuTextItem;

typedef struct ShinkiEmuTextRequest {
  uint64_t session_epoch;
  uint64_t context_epoch;
  uint64_t request_id;
  const struct ShinkiEmuTextItem *items;
  size_t item_count;
} ShinkiEmuTextRequest;

typedef struct ShinkiEmuTextRange {
  uint64_t id;
  size_t offset;
  size_t len;
} ShinkiEmuTextRange;

/**
 * Caller provides text and item storage. On CAPACITY, required capacities are
 * returned and the same request ID is retried without executing Luau/network
 * twice. Host retains exactly one result until acknowledge or cancellation.
 */
typedef struct ShinkiEmuTextResult {
  uint8_t *bytes;
  size_t byte_capacity;
  size_t byte_count;
  struct ShinkiEmuTextRange *items;
  size_t item_capacity;
  size_t item_count;
} ShinkiEmuTextResult;

typedef struct ShinkiEmuTextService {
  void *context;
  ShinkiEmuStatus (*request)(void*,
                             const struct ShinkiEmuTextRequest*,
                             struct ShinkiEmuTextResult*,
                             struct ShinkiEmuError*);
  ShinkiEmuStatus (*acknowledge)(void*, uint64_t, uint64_t, uint64_t);
  /**
   * Independent native cancellation, never queued behind the Luau VM.
   */
  void (*invalidate)(void*, uint64_t, uint64_t);
} ShinkiEmuTextService;

typedef struct ShinkiEmuWindowButton {
  uint32_t id;
  uint32_t reserved;
  struct ShinkiEmuBytes label;
} ShinkiEmuWindowButton;

/**
 * Typed bounded platform request; interpretation is selected by `kind`.
 * Window titles <=4096 bytes; modal caption/message/input <=65536 bytes.
 * Icons are <=256x256 tightly packed RGBA8. Modal buttons are <=16 items
 * with <=256-byte labels. MODAL_TEXT_INPUT distinguishes empty input from
 * a message-only dialog. Cancellation returns CANCELLED, not a button answer.
 * Modal buttons are explicit IDs; Host never invents an answer headlessly.
 * QUERY_STATE returns only fields the native platform can actually observe.
 * Focus/suspension must never be substituted for fullscreen/minimized state.
 * FULLSCREEN flags select one FULLSCREEN_* mode; exclusive width/height
 * request an exact physical display mode. Unsupported modes fail explicitly.
 * POINTER flags select independent properties; x/y use the logical stage.
 * SET_POSITION supplies both coordinates; SET_X/SET_Y preserve the other
 * coordinate. Do not combine SET_POSITION with the individual coordinate bits.
 * SIZE uses physical client pixels; POSITION uses physical screen coordinates.
 * These requests alter Host output O, never the core's raster R.
 */
typedef struct ShinkiEmuWindowRequest {
  uint64_t session_epoch;
  uint64_t context_epoch;
  uint64_t request_id;
  uint32_t kind;
  uint32_t flags;
  struct ShinkiEmuBytes text;
  struct ShinkiEmuBytes pixels;
  uint32_t width;
  uint32_t height;
  int32_t x;
  int32_t y;
  uint64_t autohide_ms;
  const struct ShinkiEmuWindowButton *buttons;
  size_t button_count;
  struct ShinkiEmuBytes caption;
  struct ShinkiEmuBytes initial_text;
} ShinkiEmuWindowRequest;

/**
 * Caller-owned output, initialized before request; service preserves data and
 * capacity and writes only initialized UTF-8 bytes and len. Input dialogs
 * require capacity >= MAX_MODAL_TEXT before displaying UI; insufficient
 * capacity returns CAPACITY without displaying or consuming a user answer.
 */
typedef struct ShinkiEmuTextBuffer {
  uint8_t *data;
  size_t capacity;
  size_t len;
} ShinkiEmuTextBuffer;

typedef struct ShinkiEmuWindowResult {
  uint32_t selected_button;
  uint32_t reserved;
  uint32_t valid_fields;
  uint32_t fullscreen;
  uint32_t width;
  uint32_t height;
  int32_t x;
  int32_t y;
  struct ShinkiEmuTextBuffer input;
  uint32_t visible;
  uint32_t minimized;
} ShinkiEmuWindowResult;

typedef struct ShinkiEmuWindowService {
  void *context;
  ShinkiEmuStatus (*request)(void*,
                             const struct ShinkiEmuWindowRequest*,
                             struct ShinkiEmuWindowResult*,
                             struct ShinkiEmuError*);
  void (*invalidate)(void*, uint64_t, uint64_t);
} ShinkiEmuWindowService;

/**
 * Host-owned cancellation exists before open returns a Session. It is required
 * even without text/window services; return 1 when this session epoch is
 * cancelled, 0 otherwise. The callback must be nonblocking and thread-safe.
 */
typedef struct ShinkiEmuCancellationService {
  void *context;
  uint32_t (*is_cancelled)(void*, uint64_t);
} ShinkiEmuCancellationService;

/**
 * Services copied during open. Cancellation is required; text/window are
 * optional. Context objects stay alive until failed open or close returns.
 * Host cancellation also wakes its blocked services; core checks cancellation
 * between initialization stages and native work, including before open returns.
 */
typedef struct ShinkiEmuHostServices {
  struct ShinkiEmuHeader header;
  struct ShinkiEmuTextService text;
  struct ShinkiEmuWindowService window;
  struct ShinkiEmuCancellationService cancellation;
} ShinkiEmuHostServices;

typedef void *ShinkiEmuModuleOwner;

typedef struct ShinkiEmuOpenRequest {
  struct ShinkiEmuHeader header;
  struct ShinkiEmuBytes game_path;
  uint64_t session_epoch;
  const struct ShinkiEmuConfigEntry *configuration;
  size_t configuration_count;
  struct ShinkiEmuWindowState window;
  struct ShinkiEmuHostServices host;
  /**
   * Opaque owner from the negotiated module-owner extension, or null when
   * the core has no cross-session owner extension.
   */
  ShinkiEmuModuleOwner module_owner;
  /**
   * 0x0 selects the core default. Nonzero values require
   * `CAP_RENDER_EXTENT` and must lie within the declared min/max; otherwise
   * open fails with INVALID_ARGUMENT.
   */
  struct ShinkiEmuExtent render_extent;
} ShinkiEmuOpenRequest;

typedef void *ShinkiEmuSession;

typedef struct ShinkiEmuSurfaceInfo {
  uint32_t kind;
  uint32_t pixel_format;
  uint32_t color;
  uint32_t alpha;
  uint32_t rows;
  struct ShinkiEmuExtent logical;
  struct ShinkiEmuExtent raster;
  uint64_t stride;
} ShinkiEmuSurfaceInfo;

typedef struct ShinkiEmuOpenResult {
  ShinkiEmuSession session;
  uint64_t context_epoch;
  uint64_t output_epoch;
  struct ShinkiEmuSurfaceInfo surface;
} ShinkiEmuOpenResult;

typedef struct ShinkiEmuFrameIdentity {
  uint64_t session_epoch;
  uint64_t output_epoch;
  uint64_t generation;
} ShinkiEmuFrameIdentity;

/**
 * Pointer coordinates are pixels in `displayed` frame, not window or logical
 * stage coordinates. Core applies its own frame-to-game/UI mapping once.
 * Physical keys use winit code names (KeyA, Digit1, Equal, Backquote, Enter).
 * Logical keys contain actual UTF-8 characters or named keys (Enter, Escape),
 * never Debug wrappers/control characters. Wheel x/y are fractional line
 * detents, not pixels. pressed/repeat and window booleans are exactly 0 or 1.
 */
typedef struct ShinkiEmuInputEvent {
  uint32_t kind;
  uint32_t pressed;
  uint32_t repeat;
  uint32_t button;
  struct ShinkiEmuBytes physical_key;
  struct ShinkiEmuBytes logical_key;
  double x;
  double y;
  struct ShinkiEmuFrameIdentity displayed;
  struct ShinkiEmuWindowState window;
} ShinkiEmuInputEvent;

typedef struct ShinkiEmuSessionState {
  uint32_t state;
  uint32_t flags;
  uint64_t context_epoch;
  uint64_t output_epoch;
  struct ShinkiEmuError error;
} ShinkiEmuSessionState;

/**
 * One independently owned immutable lease. Return it exactly once, after the
 * actual upload API no longer reads the source. The CPU owner and release
 * callback remain valid after session close; release must never access a VM,
 * device, window, session or host callback. No pixel scans are required.
 */
typedef struct ShinkiEmuCpuLease {
  struct ShinkiEmuBytes pixels;
  void *owner;
  void (*release)(void*);
} ShinkiEmuCpuLease;

typedef struct ShinkiEmuFrameSurface {
  uint32_t state;
  uint32_t reserved;
  struct ShinkiEmuFrameIdentity identity;
  uint64_t timestamp_ns;
  struct ShinkiEmuSurfaceInfo info;
  /**
   * Populated only for a Ready frame.
   */
  struct ShinkiEmuCpuLease cpu;
} ShinkiEmuFrameSurface;

/**
 * Ordinary calls are serialized by the Host. The core schedules itself; no
 * elapsed time or advance callback exists. Session panic poisons gameplay but
 * cancel/close must still perform all reachable cleanup. `close` consumes the
 * handle on every outcome and returns only after all workers and callbacks end.
 * A failed `open` leaves a null session and performs the same cleanup itself.
 * Before close, Host prevents new cancel calls and waits for in-flight cancel
 * calls to return. No call may use the consumed handle during or after close.
 * Host publishes a successful open handle under the same synchronization as
 * cancellation; if cancellation won, it cancels/closes instead of publishing.
 */
typedef struct ShinkiEmuCoreApi {
  struct ShinkiEmuHeader header;
  ShinkiEmuStatus (*initialize)(const struct ShinkiEmuDiagnostics*, struct ShinkiEmuError*);
  ShinkiEmuStatus (*describe)(struct ShinkiEmuDescriptor*, struct ShinkiEmuError*);
  ShinkiEmuStatus (*probe)(struct ShinkiEmuBytes,
                           struct ShinkiEmuProbeResult*,
                           struct ShinkiEmuError*);
  ShinkiEmuStatus (*open)(const struct ShinkiEmuOpenRequest*,
                          struct ShinkiEmuOpenResult*,
                          struct ShinkiEmuError*);
  ShinkiEmuStatus (*input)(ShinkiEmuSession,
                           const struct ShinkiEmuInputEvent*,
                           size_t,
                           struct ShinkiEmuError*);
  ShinkiEmuStatus (*control)(ShinkiEmuSession, uint32_t, struct ShinkiEmuError*);
  ShinkiEmuStatus (*state)(ShinkiEmuSession, struct ShinkiEmuSessionState*, struct ShinkiEmuError*);
  ShinkiEmuStatus (*frame)(ShinkiEmuSession,
                           struct ShinkiEmuFrameIdentity,
                           struct ShinkiEmuFrameSurface*,
                           struct ShinkiEmuError*);
  void (*cancel)(ShinkiEmuSession);
  ShinkiEmuStatus (*close)(ShinkiEmuSession, struct ShinkiEmuError*);
  /**
   * Returns a library-lifetime versioned C table, or UNSUPPORTED with null
   * output. Backend protocols are defined only with a real implementation.
   */
  ShinkiEmuStatus (*extension)(struct ShinkiEmuBytes, uint32_t, const void**);
} ShinkiEmuCoreApi;

/**
 * Query writes null on unsupported version, otherwise returns one immutable
 * module table. It performs no IO, starts no workers and installs no logger.
 */
typedef ShinkiEmuStatus (*ShinkiEmuQuery)(uint32_t, const struct ShinkiEmuCoreApi**);

/**
 * Optional ownership extension for real cross-session module resources, e.g.
 * Musica's explicit graphics Instance owner. A Host core instance creates one
 * owner after diagnostic initialization and retains it through all its sessions.
 * Other cores report UNSUPPORTED rather than constructing a dummy owner.
 *
 * create does not start a game. On error it returns null after cleanup; Host
 * also destroys a non-null failed result defensively. destroy consumes the owner
 * on every outcome, only after sessions close and callbacks finish. Both calls
 * run on the module owner thread. Libraries remain resident after destruction.
 */
typedef struct ShinkiEmuModuleOwnerApi {
  struct ShinkiEmuHeader header;
  ShinkiEmuStatus (*create)(ShinkiEmuModuleOwner*, struct ShinkiEmuError*);
  ShinkiEmuStatus (*destroy)(ShinkiEmuModuleOwner, struct ShinkiEmuError*);
} ShinkiEmuModuleOwnerApi;

/**
 * One Host-invoked session control. `control` is unique and
 * `>= CONTROL_CORE_BASE`. `label` is library-lifetime UTF-8.
 */
typedef struct ShinkiEmuSessionMenuItem {
  uint32_t control;
  uint32_t icon;
  struct ShinkiEmuBytes label;
} ShinkiEmuSessionMenuItem;

/**
 * Core-defined in-session menu. The table and labels are library-lifetime,
 * immutable, UTF-8. Invoking an item means the Host calls
 * `CoreApi.control(session, item.control)`. Controls are `>= CONTROL_CORE_BASE`
 * and unique. A core returns `INVALID_STATE` when the action is not possible
 * now; this is not a session failure.
 */
typedef struct ShinkiEmuSessionMenuApi {
  struct ShinkiEmuHeader header;
  const struct ShinkiEmuSessionMenuItem *items;
  size_t count;
} ShinkiEmuSessionMenuApi;

#define ShinkiEmuOK 0

#define ShinkiEmuUNSUPPORTED 1

#define ShinkiEmuINVALID_ARGUMENT 2

#define ShinkiEmuINVALID_STATE 3

#define ShinkiEmuCAPACITY 4

#define ShinkiEmuCANCELLED 5

#define ShinkiEmuCORE_ERROR 6

#define ShinkiEmuPANICKED 7

#define ShinkiEmuNO_MATCH 8

#define ShinkiEmuSTALE_CONTEXT 9

#endif  /* SHINKI_EMU_ABI_H */
