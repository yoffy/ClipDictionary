#include "pch.h"
#include "MainPage.h"

#include <algorithm>
#include <vector>

using namespace winrt;
using namespace Windows::UI::Xaml;
using Windows::ApplicationModel::DataTransfer::Clipboard;
using Windows::ApplicationModel::DataTransfer::DataPackageView;
using Windows::ApplicationModel::DataTransfer::StandardDataFormats;
using Windows::Storage::Pickers::FileOpenPicker;
using Windows::Storage::Pickers::PickerLocationId;
using Windows::Storage::Pickers::PickerViewMode;
using Windows::Storage::FileIO;
using Windows::Storage::StorageFile;
using Windows::Storage::Streams::DataReader;
using Windows::Storage::Streams::IBuffer;
using Windows::UI::Core::CoreWindow;
using Windows::UI::Core::CoreWindowActivationState;
using Windows::UI::Core::WindowActivatedEventArgs;
using Windows::UI::ViewManagement::ApplicationView;
using Windows::UI::ViewManagement::ApplicationViewMode;

namespace
{

	bool is_utf8(const char* buffer, size_t size)
	{
		const uint8_t* p = (const uint8_t*)buffer;

		for (size_t i = 0; i < size;)
		{
			uint8_t ch = p[i];

			// ASCIIか
			if (ch < 0x80)
			{
				++i;
				continue;
			}

			// UTF-8としたときの文字のバイト数
			size_t len = 0;
			while (ch & 0x80)
			{
				++len;
				ch <<= 1;
			}
			if (len == 1 || len >= 7)
			{
				// UTF-8なら2 <= len <= 6のはず
				return false;
			}

			// UTF-8だとみなして後続を読む
			for (size_t j = 1; j < len; ++j)
			{
				if ((p[i + j] & 0xC0) != 0x80)
				{
					// 先頭2ビットが0x80でないならUTF-8ではない
					return false;
				}
			}

			i += len;
		}

		return true;
	}

	void to_wcs(std::vector<wchar_t>& wstr, const char* buffer, size_t cLength)
	{
		UINT codePage = CP_ACP;
		DWORD flags = MB_PRECOMPOSED;

		if (is_utf8(buffer, cLength))
		{
			codePage = CP_UTF8;
			flags = 0;
		}

		// UTF-16に変換してwstrに入れる
		int wLength = MultiByteToWideChar(codePage, flags, buffer, int(cLength), nullptr, 0);
		wstr.resize(wLength + 1);
		MultiByteToWideChar(codePage, flags, buffer, int(cLength), &wstr[0], int(wstr.size()));
		// TODO: check error
		wstr[wLength] = 0;
	}

	//! 先頭と末尾にある [ \t■] を取り除く
	hstring TrimDictionary(wchar_t const * text)
	{
		const wchar_t* kRejectChars = L" \t■";
		size_t len = wcslen(text);
		size_t start = wcsspn(text, kRejectChars);
		size_t lastspn = 0;
		{
			// テキストを反転してwcsspnすることで末尾のreject文字数を得る (富豪的)
			wchar_t* rev = _wcsrev(_wcsdup(text));
			lastspn = wcsspn(rev, kRejectChars);
			free(rev);
		}
		size_t spnLen = start + lastspn;
		uint32_t innerLen = uint32_t(len > spnLen ? len - spnLen : 0);
		return hstring(&text[start], innerLen);
	}

}

namespace winrt::ClipDictionary::implementation
{
	MainPage::MainPage()
	{
		InitializeComponent();

		// クリップボード監視
		Clipboard::ContentChanged([this](IInspectable const& /* sender */, IInspectable const& /* args */)
		{
			UpdateFromClipboard();
		});

		// フローティングウィンドウ化
		Window::Current().Activated([this](IInspectable const& /* sender */, WindowActivatedEventArgs const& /* args */)
		{
			return ApplicationView::GetForCurrentView().TryEnterViewModeAsync(
				ApplicationViewMode::CompactOverlay
			);
		});
	}

	int32_t MainPage::MyProperty()
	{
		throw hresult_not_implemented();
	}

	void MainPage::MyProperty(int32_t /* value */)
	{
		throw hresult_not_implemented();
	}

	void MainPage::TextChanged(IInspectable const&, Controls::TextChangedEventArgs const&)
	{
		std::optional<hstring> description = LookUpWords(searchBox().Text());
		if (description)
		{
			descriptionBlock().Text(description.value());
		}
	}

	Windows::Foundation::IAsyncAction MainPage::OnOpenDictionary(IInspectable const&, RoutedEventArgs const&)
	{
		StorageFile file = co_await PickDictionary();
		if (!file)
		{
			return;
		}

		searchBox().IsEnabled(false);
		loadButton().IsEnabled(false);
		descriptionBlock().Text(L"読み込み中...");

		IBuffer buffer = co_await FileIO::ReadBufferAsync(file);
		co_await LoadDictionary(buffer.Length(), (const char*)buffer.data());

		descriptionBlock().Text(to_hstring(m_Dictionary.size()) + L"単語を読み込みました。");
		searchBox().IsEnabled(true);
		loadButton().IsEnabled(true);
	}

	Windows::Foundation::IAsyncOperation<Windows::Storage::StorageFile> MainPage::PickDictionary()
	{
		FileOpenPicker picker;
		picker.ViewMode(PickerViewMode::List);
		picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
		picker.FileTypeFilter().Append(L".txt");

		return picker.PickSingleFileAsync();
	}

	Windows::Foundation::IAsyncAction MainPage::LoadDictionary(size_t size, char const* buffer)
	{
		co_await resume_background();

		m_Dictionary.clear();

		std::vector<wchar_t> wstr;
		auto iLastKey = m_Dictionary.begin();
		for (size_t i = 0; i < size; ++i)
		{
			// \nの次から次の\nまでをstart, cLengthで表現
			size_t start = i + strspn(&buffer[i], "\n\r");
			size_t cLength = strcspn(&buffer[start], "\n\r");
			i += cLength;

			to_wcs(wstr, &buffer[start], cLength);

			// タブ(ejdic形式)または「:」(英辞郎形式)をキーの区切りとしてm_Dictionaryに追加
			size_t iSep = 0;
			size_t sepLen = 1;
			bool isEijiro = (wstr[0] == L'■');
			if (isEijiro)
			{
				// 「12:00 noon」のような単語があるため「 : 」で区切る
				const wchar_t sepSeq[] = L" : ";
				sepLen = _countof(sepSeq) - 1; // NUL
				auto iSep2 = std::search(wstr.begin(), wstr.end(), &sepSeq[0], &sepSeq[sepLen]);
				iSep = iSep2 - wstr.begin();
			}
			else
			{
				// タブ区切り
				iSep = wcscspn(&wstr[0], L"\t");
			}
			wstr[iSep] = 0;
			size_t iKeyStart = 0;
			do
			{
				// ejdic形式でキーが「favor,favour」のように複数ある場合はそれぞれのキーで登録
				// 英辞郎形式は「■Foo Co,. Ltd.」のように1つのキーの中に「,」が入るので分割しない
				wchar_t* pSubSep = isEijiro ? &wstr[iSep] : std::find(&wstr[iKeyStart], &wstr[iSep], L',');
				*pSubSep = 0;
				hstring key = TrimDictionary(&wstr[iKeyStart]);
				hstring value = &wstr[iSep + sepLen];
				if (m_Dictionary.size() && iLastKey->first == key)
				{
					// すでにキーが存在する場合は連結する (例: 英辞郎のfavour)
					iLastKey->second = iLastKey->second + L"\n" + value;
				}
				else
				{
					// 新規に追加
					iLastKey = m_Dictionary.emplace_hint(m_Dictionary.end(), key, value);
				}

				iKeyStart = pSubSep - &wstr[0] + 1;
			} while (iKeyStart < iSep);
		}
	}

	std::optional<hstring> MainPage::LookUpWords(hstring const & words)
	{
		auto iDesc = m_Dictionary.find(TrimDictionary(words.c_str()));
		if (iDesc == m_Dictionary.end())
		{
			return std::nullopt;
		}

		// 第一義
		hstring description = iDesc->second;

		// 他にも定義がある場合は探す
		auto iSubDesc = iDesc;
		const hstring& key = iDesc->first;

		while (++iSubDesc != m_Dictionary.end())
		{
			const hstring& subKey = iSubDesc->first;
			if (0 != memcmp(key.c_str(), subKey.c_str(), std::min(key.size(), subKey.size())))
			{
				break;
			}
			if (*subKey.rbegin() != L'}')
			{
				break;
			}
		}

		// 先頭から順に第一義に足していく
		while (++iDesc != iSubDesc)
		{
			hstring header = &iDesc->first.c_str()[key.size()]; // 「key｛名｝」のkeyを飛ばす
			description = description + L"\n" + header + iDesc->second;
		}

		return description;
	}

	Windows::Foundation::IAsyncAction MainPage::UpdateFromClipboard()
	{
		DataPackageView content = Clipboard::GetContent();
		if (content.Contains(StandardDataFormats::Text())) {
			// CLIPBRD_E_CANT_OPEN が発生することがあるので何回かトライする
			for (int i = 0; i < 4; ++i)
			{
				try {
					auto text = co_await content.GetTextAsync();
					searchBox().Text(text);
					break;
				}
				catch (const hresult_error& e) {
					if (CLIPBRD_E_CANT_OPEN != e.code()) {
						break;
					}
					Sleep(500);
				}
			}
		}
	}

}
