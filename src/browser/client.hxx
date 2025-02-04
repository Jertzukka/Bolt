#ifndef _BOLT_CLIENT_HXX_
#define _BOLT_CLIENT_HXX_
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/views/cef_window_delegate.h"
#include "app.hxx"
#include "../browser.hxx"

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#if defined(CEF_X11)
#include <xcb/xcb.h>
#endif

#if defined(BOLT_DEV_LAUNCHER_DIRECTORY)
#include "../file_manager/directory.hxx"
typedef FileManager::Directory CLIENT_FILEHANDLER;
#else
#include "../file_manager/launcher.hxx"
typedef FileManager::Launcher CLIENT_FILEHANDLER;
#endif

namespace Browser {
	/// Implementation of CefClient, CefBrowserProcessHandler, CefLifeSpanHandler, CefRequestHandler.
	/// https://github.com/chromiumembedded/cef/blob/5735/include/cef_client.h
	/// https://github.com/chromiumembedded/cef/blob/5735/include/cef_browser_process_handler.h
	/// https://github.com/chromiumembedded/cef/blob/5735/include/cef_life_span_handler.h
	/// https://github.com/chromiumembedded/cef/blob/5735/include/cef_request_handler.h
	struct Client: public CefClient, CefBrowserProcessHandler, CefLifeSpanHandler, CefRequestHandler, CLIENT_FILEHANDLER {
		Client(CefRefPtr<Browser::App>, std::filesystem::path config_dir, std::filesystem::path data_dir);

		/// Either opens a launcher window, or focuses an existing one. No more than one launcher window
		/// may be open at a given time. A launcher window will be opened automatically on startup,
		/// but this function may be used to open another after previous ones have been closed.
		void OpenLauncher();

		/// Must be called from the main thread, and windows_lock must be held when calling.
		/// Cleans up and eventually causes CefRunMessageLoop() to return.
		void Exit();

		/// Handler to be called when a new CefWindow is created. Must be called before Show()
		void OnWindowCreated(CefRefPtr<CefWindow>);

#if defined(BOLT_DEV_LAUNCHER_DIRECTORY)
		/* FileManager override */
		void OnFileChange() override;
#endif

		/* Icon functions, implementations are generated by the build system from image file contents */
		const unsigned char* GetTrayIcon() const;
		const unsigned char* GetIcon16() const;
		const unsigned char* GetIcon32() const;
		const unsigned char* GetIcon64() const;
		const unsigned char* GetIcon256() const;

		/* CefClient overrides */
		CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
		CefRefPtr<CefRequestHandler> GetRequestHandler() override;
		bool OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefProcessId, CefRefPtr<CefProcessMessage>) override;

		/* CefBrowserProcessHandler overrides */
		void OnContextInitialized() override;

		/* CefLifeSpanHandler overrides */
		bool OnBeforePopup(
			CefRefPtr<CefBrowser>,
			CefRefPtr<CefFrame>,
			const CefString&,
			const CefString&,
			CefLifeSpanHandler::WindowOpenDisposition,
			bool,
			const CefPopupFeatures&,
			CefWindowInfo&,
			CefRefPtr<CefClient>&,
			CefBrowserSettings&,
			CefRefPtr<CefDictionaryValue>&,
			bool*
		) override;
		void OnAfterCreated(CefRefPtr<CefBrowser>) override;
		bool DoClose(CefRefPtr<CefBrowser>) override;
		void OnBeforeClose(CefRefPtr<CefBrowser>) override;

		/* CefRequestHandler overrides */
		CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
			CefRefPtr<CefBrowser>,
			CefRefPtr<CefFrame>,
			CefRefPtr<CefRequest>,
			bool,
			bool,
			const CefString&,
			bool&
		) override;

		private:
			DISALLOW_COPY_AND_ASSIGN(Client);
			IMPLEMENT_REFCOUNTING(Client);

			bool is_closing;
			size_t closing_windows_remaining;
			bool show_devtools;
			std::filesystem::path config_dir;
			std::filesystem::path data_dir;

#if defined(CEF_X11)
			xcb_connection_t* xcb;
#endif

			// Mutex-locked vector - may be accessed from either UI thread (most of the time) or IO thread (GetResourceRequestHandler)
			std::vector<CefRefPtr<Browser::Window>> windows;
			std::mutex windows_lock;
	};
}

#endif
