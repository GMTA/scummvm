/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef BACKENDS_GRAPHICS_OPENGL_OPENGL_GRAPHICS_H
#define BACKENDS_GRAPHICS_OPENGL_OPENGL_GRAPHICS_H

#include "backends/graphics/opengl/opengl-sys.h"
#include "backends/graphics/opengl/framebuffer.h"
#include "backends/graphics/windowed.h"

#include "common/frac.h"
#include "common/mutex.h"
#include "common/ustr.h"

#include "graphics/surface.h"

namespace Graphics {
class Font;
} // End of namespace Graphics

namespace OpenGL {

// HACK: We use glColor in the OSD code. This might not be working on GL ES but
// we still enable it because Tizen already shipped with it. Also, the
// SurfaceSDL backend enables it and disabling it can cause issues in sdl.cpp.
#define USE_OSD 1

class Surface;
class Pipeline;
#if !USE_FORCED_GLES
class Shader;
#endif

enum {
	GFX_OPENGL = 0
};

class OpenGLGraphicsManager : virtual public WindowedGraphicsManager {
public:
	OpenGLGraphicsManager();
	virtual ~OpenGLGraphicsManager();

	// GraphicsManager API
	bool hasFeature(OSystem::Feature f) const override;
	void setFeatureState(OSystem::Feature f, bool enable) override;
	bool getFeatureState(OSystem::Feature f) const override;

	const OSystem::GraphicsMode *getSupportedGraphicsModes() const override;
	int getDefaultGraphicsMode() const override;
	bool setGraphicsMode(int mode, uint flags = OSystem::kGfxModeNoFlags) override;
	int getGraphicsMode() const override;

#ifdef USE_RGB_COLOR
	Graphics::PixelFormat getScreenFormat() const override;
	Common::List<Graphics::PixelFormat> getSupportedFormats() const override;
#endif

	const OSystem::GraphicsMode *getSupportedStretchModes() const override;
	int getDefaultStretchMode() const override;
	bool setStretchMode(int mode) override;
	int getStretchMode() const override;

#ifdef USE_SCALERS
	uint getDefaultScaler() const override;
	uint getDefaultScaleFactor() const override;
	bool setScaler(uint mode, int factor) override;
	uint getScaler() const override;
#endif

	void beginGFXTransaction() override;
	OSystem::TransactionError endGFXTransaction() override;

	int getScreenChangeID() const override;

	void initSize(uint width, uint height, const Graphics::PixelFormat *format) override;

	int16 getWidth() const override;
	int16 getHeight() const override;

	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
	void fillScreen(uint32 col) override;

	void updateScreen() override;

	Graphics::Surface *lockScreen() override;
	void unlockScreen() override;

	void setFocusRectangle(const Common::Rect& rect) override;
	void clearFocusRectangle() override;

	int16 getOverlayWidth() const override;
	int16 getOverlayHeight() const override;

	Graphics::PixelFormat getOverlayFormat() const override;

	void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;
	void clearOverlay() override;
	void grabOverlay(Graphics::Surface &surface) const override;

	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor, bool dontScale, const Graphics::PixelFormat *format) override;
	void setCursorPalette(const byte *colors, uint start, uint num) override;

	void displayMessageOnOSD(const Common::U32String &msg) override;
	void displayActivityIconOnOSD(const Graphics::Surface *icon) override;

	// PaletteManager interface
	void setPalette(const byte *colors, uint start, uint num) override;
	void grabPalette(byte *colors, uint start, uint num) const override;

protected:
	/**
	 * Whether an GLES or GLES2 context is active.
	 */
	bool isGLESContext() const { return g_context.type == kContextGLES || g_context.type == kContextGLES2; }

	/**
	 * Sets the OpenGL (ES) type the graphics manager shall work with.
	 *
	 * This needs to be called at least once (and before ever calling
	 * notifyContextCreate).
	 *
	 * @param type Type of the OpenGL (ES) contexts to be created.
	 */
	void setContextType(ContextType type);

	/**
	 * Notify the manager of a OpenGL context change. This should be the first
	 * thing to call after you created an OpenGL (ES) context!
	 *
	 * @param defaultFormat      The new default format for the game screen
	 *                           (this is used for the CLUT8 game screens).
	 * @param defaultFormatAlpha The new default format with an alpha channel
	 *                           (this is used for the overlay and cursor).
	 */
	void notifyContextCreate(const Graphics::PixelFormat &defaultFormat, const Graphics::PixelFormat &defaultFormatAlpha);

	/**
	 * Notify the manager that the OpenGL context is about to be destroyed.
	 * This will free up/reset internal OpenGL related state and *must* be
	 * called whenever a context might be created again after destroying a
	 * context.
	 */
	void notifyContextDestroy();

	/**
	 * Create a surface with the specified pixel format.
	 *
	 * @param format     The pixel format the Surface object should accept as
	 *                   input.
	 * @param wantAlpha  For CLUT8 surfaces this marks whether an alpha
	 *                   channel should be used.
	 * @param wantScaler Whether or not a software scaler should be used.
	 * @return A pointer to the surface or nullptr on failure.
	 */
	Surface *createSurface(const Graphics::PixelFormat &format, bool wantAlpha = false, bool wantScaler = false);

	//
	// Transaction support
	//
	struct VideoState {
		VideoState() : valid(false), gameWidth(0), gameHeight(0),
#ifdef USE_RGB_COLOR
		    gameFormat(),
#endif
		    aspectRatioCorrection(false), graphicsMode(GFX_OPENGL), filtering(true),
		    scalerIndex(0), scaleFactor(1) {
		}

		bool valid;

		uint gameWidth, gameHeight;
#ifdef USE_RGB_COLOR
		Graphics::PixelFormat gameFormat;
#endif
		bool aspectRatioCorrection;
		int graphicsMode;
		bool filtering;

		uint scalerIndex;
		int scaleFactor;

		bool operator==(const VideoState &right) {
			return gameWidth == right.gameWidth && gameHeight == right.gameHeight
#ifdef USE_RGB_COLOR
			    && gameFormat == right.gameFormat
#endif
			    && aspectRatioCorrection == right.aspectRatioCorrection
			    && graphicsMode == right.graphicsMode
				&& filtering == right.filtering;
		}

		bool operator!=(const VideoState &right) {
			return !(*this == right);
		}
	};

	/**
	 * The currently set up video state.
	 */
	VideoState _currentState;

	/**
	 * The old video state used when doing a transaction rollback.
	 */
	VideoState _oldState;

protected:
	enum TransactionMode {
		kTransactionNone = 0,
		kTransactionActive = 1,
		kTransactionRollback = 2
	};

	TransactionMode getTransactionMode() const { return _transactionMode; }

private:
	/**
	 * The current transaction mode.
	 */
	TransactionMode _transactionMode;

	/**
	 * The current screen change ID.
	 */
	int _screenChangeID;

	/**
	 * The current stretch mode.
	 */
	int _stretchMode;

	/**
	 * Scaled version of _gameScreenShakeXOffset and _gameScreenShakeYOffset (as a Common::Point)
	 */
	Common::Point _shakeOffsetScaled;

protected:
	/**
	 * Set up the requested video mode. This takes parameters which describe
	 * what resolution the game screen requests (this is possibly aspect ratio
	 * corrected!).
	 *
	 * A sub-class should take these parameters as hints. It might very well
	 * set up a mode which it thinks suites the situation best.
	 *
	 * @parma requestedWidth  This is the requested actual game screen width.
	 * @param requestedHeight This is the requested actual game screen height.
	 * @param format          This is the requested pixel format of the virtual game screen.
	 * @return true on success, false otherwise
	 */
	virtual bool loadVideoMode(uint requestedWidth, uint requestedHeight, const Graphics::PixelFormat &format) = 0;

	/**
	 * Refresh the screen contents.
	 */
	virtual void refreshScreen() = 0;

	/**
	 * Saves a screenshot of the entire window, excluding window decorations.
	 *
	 * @param filename The output filename.
	 * @return true on success, false otherwise
	 */
	bool saveScreenshot(const Common::String &filename) const;

	// Do not hide the argument-less saveScreenshot from the base class
	using WindowedGraphicsManager::saveScreenshot;

private:
	//
	// OpenGL utilities
	//

	/**
	 * Initialize the active context for use.
	 */
	void initializeGLContext();

	/**
	 * Render back buffer.
	 */
	Backbuffer _backBuffer;

	/**
	 * OpenGL pipeline used for rendering.
	 */
	Pipeline *_pipeline;

protected:
	/**
	 * Query the address of an OpenGL function by name.
	 *
	 * This can only be used after a context has been created.
	 * Please note that this function can return valid addresses even if the
	 * OpenGL context does not support the function.
	 *
	 * @param name The name of the OpenGL function.
	 * @return An function pointer for the requested OpenGL function or
	 *         nullptr in case of failure.
	 */
	virtual void *getProcAddress(const char *name) const = 0;

	/**
	 * Try to determine the internal parameters for a given pixel format.
	 *
	 * @return true when the format can be used, false otherwise.
	 */
	bool getGLPixelFormat(const Graphics::PixelFormat &pixelFormat, GLenum &glIntFormat, GLenum &glFormat, GLenum &glType) const;

	bool gameNeedsAspectRatioCorrection() const override;
	int getGameRenderScale() const override;
	void recalculateDisplayAreas() override;
	void handleResizeImpl(const int width, const int height) override;

	/**
	 * The default pixel format of the backend.
	 */
	Graphics::PixelFormat _defaultFormat;

	/**
	 * The default pixel format with an alpha channel.
	 */
	Graphics::PixelFormat _defaultFormatAlpha;

	/**
	 * The rendering surface for the virtual game screen.
	 */
	Surface *_gameScreen;

	/**
	 * The game palette if in CLUT8 mode.
	 */
	byte _gamePalette[3 * 256];

	//
	// Overlay
	//

	/**
	 * The rendering surface for the overlay.
	 */
	Surface *_overlay;

	//
	// Cursor
	//

	/**
	 * Set up the correct cursor palette.
	 */
	void updateCursorPalette();

	/**
	 * The rendering surface for the mouse cursor.
	 */
	Surface *_cursor;

	/**
	 * The X offset for the cursor hotspot in unscaled game coordinates.
	 */
	int _cursorHotspotX;

	/**
	 * The Y offset for the cursor hotspot in unscaled game coordinates.
	 */
	int _cursorHotspotY;

	/**
	 * Recalculate the cursor scaling. Scaling is always done according to
	 * the game screen.
	 */
	void recalculateCursorScaling();

	/**
	 * The X offset for the cursor hotspot in scaled game display area
	 * coordinates.
	 */
	int _cursorHotspotXScaled;

	/**
	 * The Y offset for the cursor hotspot in scaled game display area
	 * coordinates.
	 */
	int _cursorHotspotYScaled;

	/**
	 * The width of the cursor in scaled game display area coordinates.
	 */
	uint _cursorWidthScaled;

	/**
	 * The height of the cursor in scaled game display area coordinates.
	 */
	uint _cursorHeightScaled;

	/**
	 * The key color.
	 */
	uint32 _cursorKeyColor;

	/**
	 * Whether no cursor scaling should be applied.
	 */
	bool _cursorDontScale;

	/**
	 * Whether the special cursor palette is enabled.
	 */
	bool _cursorPaletteEnabled;

	/**
	 * The special cursor palette in case enabled.
	 */
	byte _cursorPalette[3 * 256];

#ifdef USE_SCALERS
	/**
	 * The list of scaler plugins
	 */
	const PluginList &_scalerPlugins;
#endif

#ifdef USE_OSD
	//
	// OSD
	//
protected:
	/**
	 * Returns the font used for on screen display
	 */
	virtual const Graphics::Font *getFontOSD() const;

private:
	/**
	 * Request for the OSD icon surface to be updated.
	 */
	bool _osdMessageChangeRequest;

	/**
	 * The next OSD message.
	 *
	 * If this value is not empty, the OSD message will be set
	 * to it on the next frame.
	 */
	Common::U32String _osdMessageNextData;

	/**
	 * Set the OSD message surface with the value of the next OSD message.
	 */
	void osdMessageUpdateSurface();

	/**
	 * The OSD message's contents.
	 */
	Surface *_osdMessageSurface;

	/**
	 * Current opacity level of the OSD message.
	 */
	uint8 _osdMessageAlpha;

	/**
	 * When fading the OSD message has started.
	 */
	uint32 _osdMessageFadeStartTime;

	enum {
		kOSDMessageFadeOutDelay = 2 * 1000,
		kOSDMessageFadeOutDuration = 500,
		kOSDMessageInitialAlpha = 80
	};

	/**
	 * The OSD background activity icon's contents.
	 */
	Surface *_osdIconSurface;

	enum {
		kOSDIconTopMargin = 10,
		kOSDIconRightMargin = 10
	};
#endif
};

} // End of namespace OpenGL

#endif
