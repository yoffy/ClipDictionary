//
// Declaration of the MainPage class.
//

#pragma once

#include "MainPage.g.h"
#include <map>
#include <optional>

namespace winrt::ClipDictionary::implementation
{
	struct MainPage : MainPageT<MainPage>
	{
		MainPage();

		int32_t MyProperty();
		void MyProperty(int32_t value);

		void TextChanged(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::Controls::TextChangedEventArgs const& args);
		Windows::Foundation::IAsyncAction OnOpenDictionary(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);

	private:
		std::map<hstring, hstring> m_Dictionary;

		Windows::Foundation::IAsyncOperation<Windows::Storage::StorageFile> PickDictionary();
		Windows::Foundation::IAsyncAction LoadDictionary(size_t size, char const* buffer);
		std::optional<hstring> LookUpWords(hstring const& words);
		Windows::Foundation::IAsyncAction UpdateFromClipboard();
	};
}

namespace winrt::ClipDictionary::factory_implementation
{
	struct MainPage : MainPageT<MainPage, implementation::MainPage>
	{
	};
}
