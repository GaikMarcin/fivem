#include <StdInc.h>
#include <ServerInstanceBase.h>

#include <GameServer.h>
#include <ServerConsoleHost.h>

#include <ServerGui.h>
#include <imgui.h>

#include <botan/base64.h>

#ifdef _WIN32
#include <wrl.h>

namespace WRL = Microsoft::WRL;

namespace Gdiplus
{
using std::max;
using std::min;
}

#pragma comment(lib, "gdiplus.lib")

#include <gdiplus.h>
#include <shlwapi.h>
#endif

DLL_IMPORT void RunConsoleGameFrame();

DLL_IMPORT console::Context* g_customConsoleContext;

static std::list<SvGuiModule*>* modules;

void SvGuiModule::Register(SvGuiModule* module)
{
	if (!modules)
	{
		modules = new std::remove_pointer_t<decltype(modules)>;
	}

	modules->push_back(module);

	// NOTE: not freed
	auto conVar = new ConVar<bool>(module->toggleName, ConVar_None, false, &module->toggleVar);
}

namespace fx
{
class ServerGui : public fwRefCountable, public fx::IAttached<fx::ServerInstanceBase>
{
public:
	void AttachToObject(ServerInstanceBase* object) override
	{
		m_instance = object;
		m_disableVar = m_instance->AddVariable<bool>("svgui_disable", ConVar_None, false);
		m_enableCmd = m_instance->AddCommand("svgui", [this]()
		{
			m_runConsoleHost = !m_runConsoleHost;
		});

		m_instance->GetComponent<console::Context>()->AddToBuffer(R"(
devgui_convar "Tools/Performance/Resource Monitor" resmon
devgui_convar "Tools/Network/State/Network Object Viewer" netobjviewer
)");

		Initialize();
	}

	void Initialize()
	{
		g_customConsoleContext = m_instance->GetComponent<console::Context>().GetRef();
		m_runConsoleHost = !m_disableVar->GetValue();

		m_instance->GetComponent<fx::GameServer>()->OnTick.Connect([]()
		{
			RunConsoleGameFrame();
		});

		std::thread([this]()
		{
			MainLoop();
		})
		.detach();
	}

private:
	struct SvGuiState
	{
		std::string lastIcon;
		std::string lastHostname;
	};

	void MainLoop()
	{
		// outer loop: check whether we have to run
		while (true)
		{
			std::this_thread::sleep_for(500ms);

			if (m_runConsoleHost)
			{
				ServerConsoleHost::ConHostSv consoleHost;

				SvGuiState state;

				consoleHost.Run([this, &consoleHost, &state]()
				{
					auto conCtx = m_instance->GetComponent<console::Context>();
					auto hostNameVar = conCtx->GetVariableManager()->FindEntryRaw("sv_hostname");
					auto iconVar = conCtx->GetVariableManager()->FindEntryRaw("sv_icon");
					auto gameNameVar = conCtx->GetVariableManager()->FindEntryRaw("gamename");

					if (hostNameVar && state.lastHostname != hostNameVar->GetValue())
					{
						state.lastHostname = hostNameVar->GetValue();

#ifdef _WIN32
						auto hWnd = (HWND)consoleHost.GetPlatformWindowHandle();
						SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)fmt::sprintf(L"Cfx.re Server (FXServer/%s) - %s", ToWide((gameNameVar) ? gameNameVar->GetValue() : "unknown"), ToWide(state.lastHostname)).c_str());
#endif
					}

					if (iconVar && state.lastIcon != iconVar->GetValue())
					{
						state.lastIcon = iconVar->GetValue();

#ifdef _WIN32
						auto iconData = Botan::base64_decode(state.lastIcon);

						static auto init = ([]()
						{
							ULONG_PTR gdiplusToken;

							Gdiplus::GdiplusStartupInput gdiplusStartupInput;
							Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

							return true;
						})();

						WRL::ComPtr<IStream> stream(SHCreateMemStream(iconData.data(), iconData.size()));
						if (stream.Get())
						{
							HICON hIcon, hSmallIcon;

							Gdiplus::Bitmap bitmap{ stream.Get() };
							Gdiplus::Bitmap smallBmp{ 16, 16 };
							Gdiplus::Graphics g{ &smallBmp };

							g.SetInterpolationMode(Gdiplus::InterpolationMode::InterpolationModeHighQualityBicubic);
							g.DrawImage(&bitmap, Gdiplus::Rect{ 0, 0, 16, 16 }, 0, 0, bitmap.GetWidth(), bitmap.GetHeight(), Gdiplus::UnitPixel);
							g.Flush();

							if (SUCCEEDED(bitmap.GetHICON(&hIcon)) && SUCCEEDED(smallBmp.GetHICON(&hSmallIcon)))
							{
								auto hWnd = (HWND)consoleHost.GetPlatformWindowHandle();
								SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);
								SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
							}
						}
#endif
					}

					if (modules)
					{
						for (auto& module : *modules)
						{
							if (module->toggleVar)
							{
								if (ImGui::Begin(module->name.c_str(), &module->toggleVar))
								{
									module->Render(m_instance);
								}

								ImGui::End();
							}
						}
					}

					return m_runConsoleHost;
				});

				m_runConsoleHost = false;

				trace("You can reopen the server GUI using the command ^4svgui^7.\n");
			}
		}
	}

private:
	ServerInstanceBase* m_instance = nullptr;

	bool m_runConsoleHost = false;

	std::shared_ptr<ConVar<bool>> m_disableVar;

	std::shared_ptr<ConsoleCommand> m_enableCmd;
};
}

DECLARE_INSTANCE_TYPE(fx::ServerGui);

static InitFunction initFunction([]()
{
	fx::ServerInstanceBase::OnServerCreate.Connect([](fx::ServerInstanceBase* instance)
	{
		instance->SetComponent(new fx::ServerGui());
	});
});
