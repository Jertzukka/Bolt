#include "browser.hxx"
#include "browser/client.hxx"
#include "include/views/cef_window.h"

#include <algorithm>

#include <fmt/core.h>

Browser::Window::Window(CefRefPtr<Browser::Client> client, Browser::Details details, CefString url, bool show_devtools):
	client(client.get()), show_devtools(show_devtools), details(details), window(nullptr), browser_view(nullptr), browser(nullptr), pending_child(nullptr)
{
	fmt::print("[B] Browser::Window constructor, this={}\n", reinterpret_cast<uintptr_t>(this));
	this->Init(client, details, url, show_devtools);
}

Browser::Window::Window(CefRefPtr<Browser::Client> client, Browser::Details details, bool show_devtools):
	client(client.get()), show_devtools(show_devtools), details(details), window(nullptr), browser_view(nullptr), browser(nullptr), pending_child(nullptr)
{
	fmt::print("[B] Browser::Window popup constructor, this={}\n", reinterpret_cast<uintptr_t>(this));
}

bool Browser::Window::IsLauncher() const {
	return false;
}

void Browser::Window::Init(CefRefPtr<CefClient> client, Browser::Details details, CefString url, bool show_devtools) {
	CefBrowserSettings browser_settings;
	browser_settings.background_color = CefColorSetARGB(0, 0, 0, 0);
	this->browser_view = CefBrowserView::CreateBrowserView(client, url, browser_settings, nullptr, nullptr, this);
	CefWindow::CreateTopLevelWindow(this);
}

void Browser::Window::Refresh() const {
	if (this->browser) {
		this->browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, CefProcessMessage::Create("__bolt_refresh"));
	}
}

void Browser::Window::OnWindowCreated(CefRefPtr<CefWindow> window) {
	fmt::print("[B] OnWindowCreated {} this={}\n", window->GetID(), reinterpret_cast<uintptr_t>(this));
	this->client->OnWindowCreated(window);
	this->window = std::move(window);
	this->window->AddChildView(this->browser_view);
	if (this->details.center_on_open) {
		this->window->CenterWindow(CefSize(this->details.preferred_width, this->details.preferred_height));
	}
	this->window->Show();
}

void Browser::Window::OnWindowClosing(CefRefPtr<CefWindow> window) {
	fmt::print("[B] OnWindowClosing {} this={}\n", window->GetID(), reinterpret_cast<uintptr_t>(this));
	this->window = nullptr;
	this->browser_view = nullptr;
	this->pending_child = nullptr;
}

CefRect Browser::Window::GetInitialBounds(CefRefPtr<CefWindow> window) {
	return cef_rect_t {
		.x = this->details.startx,
		.y = this->details.starty,
		.width = this->details.preferred_width,
		.height = this->details.preferred_height,
	};
}

cef_show_state_t Browser::Window::GetInitialShowState(CefRefPtr<CefWindow>) {
	return CEF_SHOW_STATE_NORMAL;
}

bool Browser::Window::IsFrameless(CefRefPtr<CefWindow>) {
	return !this->details.frame;
}

bool Browser::Window::CanResize(CefRefPtr<CefWindow>) {
	return this->details.resizeable;
}

bool Browser::Window::CanMaximize(CefRefPtr<CefWindow>) {
	return false;
}

bool Browser::Window::CanMinimize(CefRefPtr<CefWindow>) {
	return false;
}

bool Browser::Window::CanClose(CefRefPtr<CefWindow> win) {
	fmt::print("[B] CanClose for window {}, this={}\n", window->GetID(), reinterpret_cast<uintptr_t>(this));

	// Empty this->children by swapping it with a local empty vector, then iterate the local vector
	std::vector<CefRefPtr<Browser::Window>> children;
	std::swap(this->children, children);
	for (CefRefPtr<Window>& window: children) {
		window->browser->GetHost()->CloseBrowser(true);
	}

	// CEF will call CefLifeSpanHandler::DoClose (implemented in Client), giving us a chance to
	// do cleanup and then call TryCloseBrowser() a second time.
	// This strategy is suggested by official examples, e.g. cefsimple:
	// https://github.com/chromiumembedded/cef/blob/5735/tests/cefsimple/simple_app.cc#L38-L45
	CefRefPtr<CefBrowser> browser = this->browser_view->GetBrowser();
	if (browser) {
		return browser->GetHost()->TryCloseBrowser();
	} else {
		return true;
	}
}

CefSize Browser::Window::GetPreferredSize(CefRefPtr<CefView>) {
	return CefSize(this->details.preferred_width, this->details.preferred_height);
}

CefRefPtr<CefBrowserViewDelegate> Browser::Window::GetDelegateForPopupBrowserView(CefRefPtr<CefBrowserView>, const CefBrowserSettings&, CefRefPtr<CefClient>, bool is_devtools) {
	fmt::print("[B] GetDelegateForPopupBrowserView this={}\n", reinterpret_cast<uintptr_t>(this));
	Browser::Details details = {
		.preferred_width = this->popup_features.widthSet ? this->popup_features.width : 800,
		.preferred_height = this->popup_features.heightSet ? this->popup_features.height : 608,
		.startx = this->popup_features.x,
		.starty = this->popup_features.y,
		.center_on_open = !this->popup_features.xSet || !this->popup_features.ySet,
		.resizeable = true,
		.frame = true,
		.is_devtools = is_devtools,
	};
	this->pending_child = new Browser::Window(this->client, details, this->show_devtools);
	return this->pending_child;
}

bool Browser::Window::OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view, CefRefPtr<CefBrowserView> popup_browser_view, bool is_devtools) {
	fmt::print("[B] OnPopupBrowserViewCreated this={}\n", reinterpret_cast<uintptr_t>(this));
	this->pending_child->browser_view = popup_browser_view;
	CefWindow::CreateTopLevelWindow(this->pending_child);
	this->children.push_back(this->pending_child);
	CefRefPtr<Browser::Window> child = nullptr;
	std::swap(this->pending_child, child);
	return true;
}

void Browser::Window::OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view, CefRefPtr<CefBrowser> browser) {
	fmt::print("[B] OnBrowserCreated this={} {}\n", reinterpret_cast<uintptr_t>(this), browser->GetIdentifier());
	this->browser = browser;
	if (this->show_devtools && !this->details.is_devtools) {
		this->ShowDevTools();
	}
}

void Browser::Window::OnBrowserDestroyed(CefRefPtr<CefBrowserView>, CefRefPtr<CefBrowser>) {
	fmt::print("[B] OnBrowserDestroyed this={}\n", reinterpret_cast<uintptr_t>(this));
	this->browser = nullptr;
}

CefRefPtr<CefResourceRequestHandler> Browser::Window::GetResourceRequestHandler(
	CefRefPtr<CefBrowser>,
	CefRefPtr<CefFrame>,
	CefRefPtr<CefRequest>,
	bool,
	bool,
	const CefString&,
	bool&
) {
	// Custom resource handling is implemented by overriding this function in child classes
	return nullptr;
}

cef_chrome_toolbar_type_t Browser::Window::GetChromeToolbarType() {
	return CEF_CTT_NONE;
}

bool Browser::Window::HasBrowser(CefRefPtr<CefBrowser> browser) const {
	if (this->browser->IsSame(browser)) {
		return true;
	}
	bool ret = std::any_of(
		this->children.begin(),
		this->children.end(),
		[&browser](const CefRefPtr<Browser::Window>& window){ return window->HasBrowser(browser); });
	return ret;
}

size_t Browser::Window::CountBrowsers() const {
	size_t ret = 1;
	for (const CefRefPtr<Browser::Window>& child: this->children) {
		ret += child->CountBrowsers();
	}
	return ret;
}

void Browser::Window::Focus() const {
	CefRefPtr<CefBrowserHost> host = this->browser->GetHost();
	if (host) {
		host->SetFocus(true);
	}
}

void Browser::Window::Close() {
	fmt::print("[B] Close this={}\n", reinterpret_cast<uintptr_t>(this));
	// Children will be closed when CEF calls DoClose for this instance
	this->browser->GetHost()->CloseBrowser(true);
}

void Browser::Window::CloseChildrenExceptDevtools() {
	for (CefRefPtr<Browser::Window>& window: this->children) {
		if (!window->details.is_devtools) {
			window->Close();
		}
	}
}

bool Browser::Window::OnBrowserClosing(CefRefPtr<CefBrowser> browser) {
	fmt::print("[B] OnBrowserClosing this={}\n", reinterpret_cast<uintptr_t>(this));
	this->children.erase(
		std::remove_if(
			this->children.begin(),
			this->children.end(),
			[&browser](const CefRefPtr<Browser::Window>& window){ return window->OnBrowserClosing(browser); }
		),
		this->children.end()
	);
	return this->browser->IsSame(browser);
}

void Browser::Window::SetPopupFeaturesForBrowser(CefRefPtr<CefBrowser> browser, const CefPopupFeatures& popup_features) {
	for (CefRefPtr<Window>& window: this->children) {
		window->SetPopupFeaturesForBrowser(browser, popup_features);
	}
	if (this->browser_view->GetBrowser()->IsSame(browser)) {
		this->popup_features = popup_features;
	}
}

void Browser::Window::ShowDevTools() {
	CefRefPtr<CefBrowserHost> browser_host = browser->GetHost();
	CefWindowInfo window_info; // ignored, because this is a BrowserView
	CefBrowserSettings browser_settings;
	browser_host->ShowDevTools(window_info, browser_host->GetClient(), browser_settings, CefPoint());
}
