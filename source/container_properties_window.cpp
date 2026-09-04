//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "container_properties_window.h"

#include "old_properties_window.h"
#include "properties_window.h"
#include "find_item_window.h"
#include "gui.h"
#include "settings.h"
#include <wx/listbox.h>
#include "complexitem.h"
#include "map.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <exception>
#include <iomanip>
#include <cctype>

#include <wx/dcmemory.h>
#include <wx/numdlg.h>
#include <wx/colour.h>
#include <wx/choicdlg.h>
#include <wx/textdlg.h>

// ============================================================================
// Container Item Button
// Displayed in the container object properties menu, needs some
// custom event handling for the right-click menu etcetera so we
// need to define a custom class for it.

namespace {
	struct FavoriteBucket {
		std::string name;
		std::vector<uint16_t> items;
	};

	using FavoriteBuckets = std::vector<FavoriteBucket>;

	enum class FavoriteCommandAction {
		AddItem,
		AddRandomFromBucket,
	};

	struct FavoriteCommand {
		FavoriteCommandAction action;
		std::string bucketName;
		uint16_t itemId;
	};

	struct FavoriteCommandRegistry {
		void Reset() {
			commands.clear();
			nextId = CONTAINER_POPUP_MENU_FAVORITE_FIRST;
		}

		int Allocate(FavoriteCommand command) {
			if (nextId > CONTAINER_POPUP_MENU_FAVORITE_LAST) {
				return wxID_NONE;
			}

			const int id = nextId++;
			commands.emplace(id, std::move(command));
			return id;
		}

		const FavoriteCommand* Find(int id) const {
			auto it = commands.find(id);
			if (it == commands.end()) {
				return nullptr;
			}
			return &it->second;
		}

		std::unordered_map<int, FavoriteCommand> commands;
		int nextId = CONTAINER_POPUP_MENU_FAVORITE_FIRST;
	};

	FavoriteCommandRegistry g_favoriteCommandRegistry;

	std::string TrimString(const std::string& value) {
		size_t start = 0;
		while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
			++start;
		}

		size_t end = value.size();
		while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
			--end;
		}

		return value.substr(start, end - start);
	}

	std::string EncodeBucketName(const std::string& name) {
		std::ostringstream encoded;
		encoded << std::uppercase << std::hex;
		for (unsigned char ch : name) {
			if (ch == '%' || ch == ';' || ch == ',' || ch == '=') {
				encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
			} else {
				encoded << static_cast<char>(ch);
			}
		}
		return encoded.str();
	}

	std::string DecodeBucketName(const std::string& value) {
		std::string result;
		for (size_t i = 0; i < value.size();) {
			if (value[i] == '%' && i + 2 < value.size() && std::isxdigit(static_cast<unsigned char>(value[i + 1])) && std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
				const std::string hex = value.substr(i + 1, 2);
				try {
					const int decoded = std::stoi(hex, nullptr, 16);
					result.push_back(static_cast<char>(decoded));
				} catch (const std::exception&) {
					// Ignore malformed escape sequences
				}
				i += 3;
			} else {
				result.push_back(value[i]);
				++i;
			}
		}
		return result;
	}

	std::vector<uint16_t> ParseFavoriteItems(const std::string& value) {
		std::vector<uint16_t> items;
		std::unordered_set<uint16_t> seen;
		std::stringstream ss(value);
		std::string token;
		while (std::getline(ss, token, ',')) {
			token = TrimString(token);
			if (token.empty()) {
				continue;
			}

			try {
				const int parsed = std::stoi(token);
				if (parsed < 0 || parsed > std::numeric_limits<uint16_t>::max()) {
					continue;
				}

				const uint16_t id = static_cast<uint16_t>(parsed);
				if (!g_items.typeExists(id) || seen.count(id) != 0) {
					continue;
				}

				seen.insert(id);
				items.push_back(id);
			} catch (const std::exception&) {
				// Ignore malformed entries
			}
		}
		return items;
	}

	FavoriteBuckets LoadContainerFavoriteBuckets() {
		FavoriteBuckets buckets;
		std::string value = g_settings.getString(Config::CONTAINER_FAVORITE_ITEMS);
		if (value.empty()) {
			return buckets;
		}

		if (value.find('=') == std::string::npos) {
			std::vector<uint16_t> legacyFavorites = ParseFavoriteItems(value);
			if (!legacyFavorites.empty()) {
				FavoriteBucket bucket;
				bucket.name = "Favorites";
				bucket.items = std::move(legacyFavorites);
				buckets.push_back(std::move(bucket));
			}
			return buckets;
		}

		std::stringstream ss(value);
		std::string token;
		std::unordered_set<std::string> seenNames;
		while (std::getline(ss, token, ';')) {
			if (token.empty()) {
				continue;
			}

			const size_t separator = token.find('=');
			const std::string encodedName = separator == std::string::npos ? token : token.substr(0, separator);
			const std::string itemsValue = separator == std::string::npos ? std::string() : token.substr(separator + 1);

			std::string bucketName = TrimString(DecodeBucketName(encodedName));
			if (bucketName.empty()) {
				continue;
			}

			const std::string loweredName = as_lower_str(bucketName);
			if (seenNames.count(loweredName) != 0) {
				continue;
			}

			FavoriteBucket bucket;
			bucket.name = bucketName;
			bucket.items = ParseFavoriteItems(itemsValue);
			seenNames.insert(loweredName);
			buckets.push_back(std::move(bucket));
		}

		return buckets;
	}

	void StoreContainerFavoriteBuckets(const FavoriteBuckets& buckets) {
		std::ostringstream ss;
		bool firstBucket = true;
		for (const FavoriteBucket& bucket : buckets) {
			if (bucket.name.empty()) {
				continue;
			}

			if (!firstBucket) {
				ss << ';';
			}
			firstBucket = false;

			ss << EncodeBucketName(bucket.name) << '=';

			bool firstItem = true;
			for (uint16_t id : bucket.items) {
				if (!g_items.typeExists(id)) {
					continue;
				}

				if (!firstItem) {
					ss << ',';
				}
				firstItem = false;
				ss << id;
			}
		}

		g_settings.setString(Config::CONTAINER_FAVORITE_ITEMS, ss.str());
		g_settings.save();
	}

	bool IsFavorite(uint16_t id, const FavoriteBuckets& buckets) {
		for (const FavoriteBucket& bucket : buckets) {
			if (std::find(bucket.items.begin(), bucket.items.end(), id) != bucket.items.end()) {
				return true;
			}
		}
		return false;
	}

	const FavoriteBucket* FindBucket(const FavoriteBuckets& buckets, const std::string& name) {
		const std::string lowered = as_lower_str(name);
		for (const FavoriteBucket& bucket : buckets) {
			if (as_lower_str(bucket.name) == lowered) {
				return &bucket;
			}
		}
		return nullptr;
	}

	FavoriteBucket* FindBucket(FavoriteBuckets& buckets, const std::string& name) {
		const std::string lowered = as_lower_str(name);
		for (FavoriteBucket& bucket : buckets) {
			if (as_lower_str(bucket.name) == lowered) {
				return &bucket;
			}
		}
		return nullptr;
	}

	std::string PromptForNewBucketName(wxWindow* parent, const FavoriteBuckets& buckets);

	// Adds itemId to a bucket chosen (or created) interactively; returns whether buckets was modified.
	bool AddItemIdToBucketsInteractive(wxWindow* parent, uint16_t itemId, FavoriteBuckets& buckets) {
		if (buckets.empty()) {
			std::string newBucket = PromptForNewBucketName(parent, buckets);
			if (newBucket.empty()) {
				return false;
			}

			FavoriteBucket bucket;
			bucket.name = newBucket;
			bucket.items.push_back(itemId);
			buckets.push_back(std::move(bucket));
			return true;
		}

		wxArrayString options;
		for (const FavoriteBucket& bucket : buckets) {
			options.Add(wxstr(bucket.name));
		}
		options.Add("Create new bucket...");

		wxSingleChoiceDialog dialog(parent, "Choose a favorites bucket for this item:", "Add Favorite", options);

		if (dialog.ShowModal() != wxID_OK) {
			return false;
		}

		const int selection = dialog.GetSelection();
		if (selection == wxNOT_FOUND) {
			return false;
		}

		if (selection == static_cast<int>(buckets.size())) {
			std::string newBucket = PromptForNewBucketName(parent, buckets);
			if (newBucket.empty()) {
				return false;
			}

			FavoriteBucket bucket;
			bucket.name = newBucket;
			bucket.items.push_back(itemId);
			buckets.push_back(std::move(bucket));
			return true;
		}

		auto& items = buckets[selection].items;
		if (std::find(items.begin(), items.end(), itemId) == items.end()) {
			items.push_back(itemId);
			return true;
		}
		return false;
	}

	std::string PromptForNewBucketName(wxWindow* parent, const FavoriteBuckets& buckets) {
		wxTextEntryDialog dialog(parent, "Enter a name for the favorites bucket:", "Create Favorites Bucket");
		while (dialog.ShowModal() == wxID_OK) {
			std::string name = TrimString(nstr(dialog.GetValue()));
			if (name.empty()) {
				g_gui.PopupDialog(parent, "Invalid bucket name", "Bucket name cannot be empty.", wxOK | wxICON_WARNING);
				continue;
			}

			if (FindBucket(buckets, name)) {
				g_gui.PopupDialog(parent, "Duplicate bucket", "A bucket with that name already exists.", wxOK | wxICON_WARNING);
				continue;
			}

			return name;
		}

		return std::string();
	}
	wxString FavoriteLabel(uint16_t id) {
		const ItemType& type = g_items.getItemType(id);
		if (!type.name.empty()) {
			std::string label = type.name;
			if (!type.editorsuffix.empty()) {
				label += type.editorsuffix;
			}
			return wxstr(label) + wxString::Format(" (%u)", id);
		}
		return wxString::Format("Item %u", id);
	}

	wxBitmap FavoriteBitmap(uint16_t id) {
		static std::unordered_map<uint16_t, wxBitmap> bitmapCache;

		auto it = bitmapCache.find(id);
		if (it != bitmapCache.end() && it->second.IsOk()) {
			return it->second;
		}

		const ItemType& type = g_items.getItemType(id);
		GameSprite* sprite = type.sprite;
		if (!sprite) {
			return wxBitmap();
		}

		const int bitmapSize = 32;
		wxBitmap bitmap(bitmapSize, bitmapSize);
		wxMemoryDC dc;
		dc.SelectObject(bitmap);
		const int bgshade = g_settings.getInteger(Config::ICON_BACKGROUND);
		if (bgshade >= 0) {
			dc.SetBackground(wxBrush(wxColour(bgshade, bgshade, bgshade)));
			dc.Clear();
		}
		sprite->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, bitmapSize, bitmapSize);
		dc.SelectObject(wxNullBitmap);

		bitmapCache[id] = bitmap;
		return bitmap;
	}

	void InsertItemIntoContainer(Container* container, wxWeakRef<ObjectPropertiesWindowBase> propertyWindow, size_t insertionIndex, uint16_t itemId) {
		if (!container || itemId == 0) {
			return;
		}

		if (container->getVolume() <= container->getVector().size()) {
			return;
		}

		ItemVector& itemVector = container->getVector();
		Item* item = Item::Create(itemId);
		if (!item) {
			return;
		}

		const size_t targetIndex = std::min(insertionIndex, itemVector.size());
		itemVector.insert(itemVector.begin() + targetIndex, item);

		if (propertyWindow && !propertyWindow->IsBeingDeleted()) {
			propertyWindow->Update();
		}
	}
}

std::unique_ptr<ContainerItemPopupMenu> ContainerItemButton::popup_menu;

BEGIN_EVENT_TABLE(ContainerItemButton, ItemButton)
EVT_LEFT_DOWN(ContainerItemButton::OnMouseDoubleLeftClick)
EVT_RIGHT_UP(ContainerItemButton::OnMouseRightRelease)

EVT_MENU(CONTAINER_POPUP_MENU_ADD, ContainerItemButton::OnAddItem)
EVT_MENU_RANGE(CONTAINER_POPUP_MENU_FAVORITE_FIRST, CONTAINER_POPUP_MENU_FAVORITE_LAST, ContainerItemButton::OnFavoriteMenu)
EVT_MENU(CONTAINER_POPUP_MENU_EDIT, ContainerItemButton::OnEditItem)
EVT_MENU(CONTAINER_POPUP_MENU_REMOVE, ContainerItemButton::OnRemoveItem)
EVT_MENU(CONTAINER_POPUP_MENU_TOGGLE_FAVORITE, ContainerItemButton::OnToggleFavorite)
EVT_MENU(CONTAINER_POPUP_MENU_REMOVE_ALL, ContainerItemButton::OnRemoveAllItems)
EVT_MENU(CONTAINER_POPUP_MENU_ADD_RANDOM_FAVORITES, ContainerItemButton::OnAddRandomFavorites)
EVT_MENU(CONTAINER_POPUP_MENU_MANAGE_FAVORITES, ContainerItemButton::OnManageFavorites)
END_EVENT_TABLE()

ContainerItemButton::ContainerItemButton(wxWindow* parent, bool large, int _index, const Map* map, Item* item) :
	ItemButton(parent, (large ? RENDER_SIZE_32x32 : RENDER_SIZE_16x16), (item ? item->getClientID() : 0)),
	edit_map(map),
	edit_item(item),
	index(_index) {
	////
}

ContainerItemButton::~ContainerItemButton() {
	////
}

void ContainerItemButton::OnMouseDoubleLeftClick(wxMouseEvent& WXUNUSED(event)) {
	wxCommandEvent dummy;

	if (edit_item) {
		OnEditItem(dummy);
		return;
	}

	Container* container = getParentContainer();
	if (container->getVolume() > container->getItemCount()) {
		OnAddItem(dummy);
	}
}

void ContainerItemButton::OnMouseRightRelease(wxMouseEvent& WXUNUSED(event)) {
	if (!popup_menu) {
		popup_menu.reset(newd ContainerItemPopupMenu);
	}

	popup_menu->Update(this);
	PopupMenu(popup_menu.get());
}

void ContainerItemButton::OnAddItem(wxCommandEvent& WXUNUSED(event)) {
	FindItemDialog dialog(GetParent(), "Choose Item to add", true);

	if (g_settings.getBoolean(Config::CONTAINER_FIND_DEFAULT_NAMES)) {
		dialog.setSearchMode(FindItemDialog::SearchMode::Names);
	}

	if (dialog.ShowModal() == wxID_OK) {
		Container* container = getParentContainer();
		ItemVector& itemVector = container->getVector();

		Item* item = Item::Create(dialog.getResultID());
		if (index < itemVector.size()) {
			itemVector.insert(itemVector.begin() + index, item);
		} else {
			itemVector.push_back(item);
		}

		ObjectPropertiesWindowBase* propertyWindow = getParentContainerWindow();
		if (propertyWindow) {
			propertyWindow->Update();
		}
	}
	dialog.Destroy();
}

void ContainerItemButton::OnFavoriteMenu(wxCommandEvent& event) {
	const FavoriteCommand* command = g_favoriteCommandRegistry.Find(event.GetId());
	if (!command) {
		return;
	}

	switch (command->action) {
		case FavoriteCommandAction::AddItem:
			AddItemToContainer(command->itemId);
			break;
		case FavoriteCommandAction::AddRandomFromBucket:
			AddRandomFavoritesFromBucket(command->bucketName);
			break;
	}
}

void ContainerItemButton::OnEditItem(wxCommandEvent& WXUNUSED(event)) {
	ASSERT(edit_item);

	wxPoint newDialogAt;
	wxWindow* w = this;
	while ((w = w->GetParent())) {
		if (ObjectPropertiesWindowBase* o = dynamic_cast<ObjectPropertiesWindowBase*>(w)) {
			newDialogAt = o->GetPosition();
			break;
		}
	}

	newDialogAt += FromDIP(wxPoint(20, 20));

	wxDialog* d;

	if (edit_map->getVersion().otbm >= MAP_OTBM_4) {
		d = newd PropertiesWindow(this, edit_map, nullptr, edit_item, newDialogAt);
	} else {
		d = newd OldPropertiesWindow(this, edit_map, nullptr, edit_item, newDialogAt);
	}

	d->ShowModal();
	d->Destroy();
}

void ContainerItemButton::OnRemoveItem(wxCommandEvent& WXUNUSED(event)) {
	ASSERT(edit_item);
	int32_t ret = g_gui.PopupDialog(GetParent(), "Remove Item", "Are you sure you want to remove this item from the container?", wxYES | wxNO);

	if (ret != wxID_YES) {
		return;
	}

	Container* container = getParentContainer();
	ItemVector& itemVector = container->getVector();

	auto it = itemVector.begin();
	for (; it != itemVector.end(); ++it) {
		if (*it == edit_item) {
			break;
		}
	}

	ASSERT(it != itemVector.end());

	itemVector.erase(it);
	delete edit_item;

	ObjectPropertiesWindowBase* propertyWindow = getParentContainerWindow();
	if (propertyWindow) {
		propertyWindow->Update();
	}
}

void ContainerItemButton::OnToggleFavorite(wxCommandEvent& WXUNUSED(event)) {
	if (!edit_item) {
		return;
	}

	FavoriteBuckets buckets = LoadContainerFavoriteBuckets();
	const uint16_t itemId = edit_item->getID();

	std::vector<size_t> containingIndices;
	for (size_t i = 0; i < buckets.size(); ++i) {
		auto& items = buckets[i].items;
		if (std::find(items.begin(), items.end(), itemId) != items.end()) {
			containingIndices.push_back(i);
		}
	}

	bool modified = false;

	if (!containingIndices.empty()) {
		wxArrayString options;
		for (size_t index : containingIndices) {
			options.Add(wxstr(buckets[index].name));
		}
		options.Add("Remove from all buckets");

		wxSingleChoiceDialog dialog(this, "Select which favorites bucket to remove this item from:", "Remove Favorite", options);

		if (dialog.ShowModal() != wxID_OK) {
			return;
		}

		const int selection = dialog.GetSelection();
		if (selection == wxNOT_FOUND) {
			return;
		}

		if (selection == static_cast<int>(containingIndices.size())) {
			for (size_t index : containingIndices) {
				auto& items = buckets[index].items;
				auto removeIt = std::remove(items.begin(), items.end(), itemId);
				if (removeIt != items.end()) {
					items.erase(removeIt, items.end());
					modified = true;
				}
			}
		} else {
			auto& items = buckets[containingIndices[selection]].items;
			auto removeIt = std::remove(items.begin(), items.end(), itemId);
			if (removeIt != items.end()) {
				items.erase(removeIt, items.end());
				modified = true;
			}
		}
	} else {
		modified = AddItemIdToBucketsInteractive(this, itemId, buckets);
	}

	if (modified) {
		StoreContainerFavoriteBuckets(buckets);
	}
}

void ContainerItemButton::OnManageFavorites(wxCommandEvent& WXUNUSED(event)) {
	FindItemDialog dialog(GetParent(), "Choose Item to Favorite", true);

	if (g_settings.getBoolean(Config::CONTAINER_FIND_DEFAULT_NAMES)) {
		dialog.setSearchMode(FindItemDialog::SearchMode::Names);
	}

	if (dialog.ShowModal() == wxID_OK) {
		const uint16_t itemId = static_cast<uint16_t>(dialog.getResultID());
		FavoriteBuckets buckets = LoadContainerFavoriteBuckets();
		if (AddItemIdToBucketsInteractive(this, itemId, buckets)) {
			StoreContainerFavoriteBuckets(buckets);
		}
	}

	dialog.Destroy();
}

void ContainerItemButton::OnRemoveAllItems(wxCommandEvent& WXUNUSED(event)) {
	Container* container = getParentContainer();
	if (!container) {
		return;
	}

	ItemVector& itemVector = container->getVector();
	if (itemVector.empty()) {
		return;
	}

	int ret = g_gui.PopupDialog("Remove All Items", "Are you sure you want to remove all items from this container?", wxYES | wxNO | wxCANCEL);
	if (ret != wxID_YES) {
		return;
	}

	for (Item* item : itemVector) {
		delete item;
	}
	itemVector.clear();

	ObjectPropertiesWindowBase* propertyWindow = getParentContainerWindow();
	if (propertyWindow) {
		propertyWindow->Update();
	}
}

void ContainerItemButton::OnAddRandomFavorites(wxCommandEvent& WXUNUSED(event)) {
	FavoriteBuckets buckets = LoadContainerFavoriteBuckets();
	if (buckets.empty()) {
		return;
	}

	wxArrayString bucketChoices;
	for (const FavoriteBucket& bucket : buckets) {
		if (bucket.items.empty()) {
			continue;
		}
		bucketChoices.Add(wxstr(bucket.name));
	}

	if (bucketChoices.empty()) {
		return;
	}

	if (bucketChoices.size() == 1) {
		AddRandomFavoritesFromBucket(nstr(bucketChoices[0]));
		return;
	}

	wxSingleChoiceDialog dialog(this, "Select the favorites bucket to use for random items:", "Add Random Favorites", bucketChoices);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	AddRandomFavoritesFromBucket(nstr(dialog.GetStringSelection()));
}

void ContainerItemButton::setItem(Item* item) {
	edit_item = item;
	if (edit_item) {
		SetSprite(edit_item->getClientID());
	} else {
		SetSprite(0);
	}
}

void ContainerItemButton::AddRandomFavoritesFromBucket(const std::string& bucketName) {
	Container* container = getParentContainer();
	if (!container) {
		return;
	}

	FavoriteBuckets buckets = LoadContainerFavoriteBuckets();
	FavoriteBucket* bucket = FindBucket(buckets, bucketName);
	if (!bucket || bucket->items.empty()) {
		return;
	}

	const size_t currentSize = container->getVector().size();
	const size_t capacity = container->getVolume();
	if (currentSize >= capacity) {
		return;
	}

	std::vector<uint16_t> favorites = bucket->items;
	favorites.erase(std::remove_if(favorites.begin(), favorites.end(), [](uint16_t id) {
						return !g_items.typeExists(id);
					}),
					favorites.end());

	if (favorites.empty()) {
		return;
	}

	const size_t availableSlots = capacity - currentSize;
	const size_t maxAdd = std::min(availableSlots, favorites.size());
	wxString bucketLabel = wxstr(bucket->name);
	wxString message = wxString::Format("How many favorite items from \"%s\" would you like to add?", bucketLabel);
	wxString title = wxString::Format("Add Random Favorites (%s)", bucketLabel);
	long count = wxGetNumberFromUser(message, "Count:", title, 1, 1, static_cast<long>(maxAdd), this);
	if (count < 1) {
		return;
	}

	std::vector<uint16_t> pool = favorites;
	for (long i = 0; i < count && !pool.empty(); ++i) {
		const int index = uniform_random(0, static_cast<int>(pool.size() - 1));
		if (!AddItemToContainer(pool[index])) {
			break;
		}
		pool.erase(pool.begin() + index);
	}
}

bool ContainerItemButton::AddItemToContainer(uint16_t itemId) {
	Container* container = getParentContainer();
	if (!container) {
		return false;
	}

	wxWeakRef<ObjectPropertiesWindowBase> propertyWindow(getParentContainerWindow());
	const size_t previousSize = container->getVector().size();
	InsertItemIntoContainer(container, propertyWindow, index, itemId);
	return container->getVector().size() > previousSize;
}

ObjectPropertiesWindowBase* ContainerItemButton::getParentContainerWindow() {
	for (wxWindow* window = GetParent(); window != nullptr; window = window->GetParent()) {
		ObjectPropertiesWindowBase* propertyWindow = dynamic_cast<ObjectPropertiesWindowBase*>(window);
		if (propertyWindow) {
			return propertyWindow;
		}
	}
	return nullptr;
}

Container* ContainerItemButton::getParentContainer() {
	ObjectPropertiesWindowBase* propertyWindow = getParentContainerWindow();
	if (propertyWindow) {
		return dynamic_cast<Container*>(propertyWindow->getItemBeingEdited());
	}
	return nullptr;
}

// ContainerItemPopupMenu
ContainerItemPopupMenu::ContainerItemPopupMenu() : wxMenu("") {
	////
}

ContainerItemPopupMenu::~ContainerItemPopupMenu() {
	////
}

void ContainerItemPopupMenu::Update(ContainerItemButton* btn) {
	// Clear the menu of all items
	while (GetMenuItemCount() != 0) {
		wxMenuItem* m_item = FindItemByPosition(0);
		if (wxMenu* submenu = m_item->GetSubMenu()) {
			m_item->SetSubMenu(nullptr);
			delete submenu;
		}
		Delete(m_item);
	}

	Container* parentContainer = btn->getParentContainer();
	const bool canAddMoreItems = parentContainer && parentContainer->getVolume() > parentContainer->getVector().size();

	FavoriteBuckets buckets = LoadContainerFavoriteBuckets();
	const bool hasBucketsWithItems = std::any_of(buckets.begin(), buckets.end(), [](const FavoriteBucket& bucket) {
		return !bucket.items.empty();
	});
	g_favoriteCommandRegistry.Reset();

	if (btn->edit_item) {
		Append(CONTAINER_POPUP_MENU_EDIT, "&Edit Item", "Open the properties menu for this item");
	}

	wxMenuItem* addItem = Append(CONTAINER_POPUP_MENU_ADD, "&Add Item", "Add a new item to the container");
	if (!canAddMoreItems) {
		addItem->Enable(false);
	}

	Append(CONTAINER_POPUP_MENU_MANAGE_FAVORITES, "Add to &Favorites...", "Choose any item and add it to a favorites bucket, whether or not it's already in this container");

	if (!buckets.empty()) {
		wxMenu* favoritesMenu = newd wxMenu();
		bool commandLimitReached = false;

		for (const FavoriteBucket& bucket : buckets) {
			wxMenu* bucketMenu = newd wxMenu();
			int bucketCommandCount = 0;
			wxString bucketLabel = wxstr(bucket.name);
			for (uint16_t id : bucket.items) {
				int commandId = g_favoriteCommandRegistry.Allocate({ FavoriteCommandAction::AddItem, bucket.name, id });
				if (commandId == wxID_NONE) {
					commandLimitReached = true;
					break;
				}

				wxMenuItem* favoriteItem = bucketMenu->Append(commandId, FavoriteLabel(id));
				wxBitmap bitmap = FavoriteBitmap(id);
				if (bitmap.IsOk()) {
					favoriteItem->SetBitmap(bitmap);
				}
				if (!canAddMoreItems) {
					favoriteItem->Enable(false);
				}
				++bucketCommandCount;
			}

			if (bucketCommandCount == 0) {
				wxMenuItem* placeholder = bucketMenu->Append(wxID_ANY, "(No favorites)");
				placeholder->Enable(false);
			}

			favoritesMenu->AppendSubMenu(bucketMenu, bucketLabel, wxString::Format("Add a favorite item from \"%s\"", bucketLabel));

			if (commandLimitReached) {
				break;
			}
		}

		bool randomCommandsAdded = false;
		if (!commandLimitReached) {
			wxMenu* randomFromMenu = newd wxMenu();
			bool commandRangeExceeded = false;
			for (const FavoriteBucket& bucket : buckets) {
				if (bucket.items.empty()) {
					continue;
				}

				int commandId = g_favoriteCommandRegistry.Allocate({ FavoriteCommandAction::AddRandomFromBucket, bucket.name, 0 });
				if (commandId == wxID_NONE) {
					commandRangeExceeded = true;
					break;
				}

				wxMenuItem* item = randomFromMenu->Append(commandId, wxstr(bucket.name));
				if (!canAddMoreItems) {
					item->Enable(false);
				}
				randomCommandsAdded = true;
			}

			if (commandRangeExceeded) {
				delete randomFromMenu;
			} else if (randomCommandsAdded) {
				wxMenu* randomMenu = newd wxMenu();
				randomMenu->AppendSubMenu(randomFromMenu, "&From bucket", "Add random favorites from a specific bucket");
				favoritesMenu->AppendSeparator();
				favoritesMenu->AppendSubMenu(randomMenu, "Add &random favorites", "Add multiple random favorites to the container");
			} else {
				delete randomFromMenu;
			}
		}

		wxMenuItem* dialogItem = favoritesMenu->Append(CONTAINER_POPUP_MENU_ADD_RANDOM_FAVORITES, "Add random favorites...", "Add multiple random favorites to the container");
		if (!canAddMoreItems || !hasBucketsWithItems) {
			dialogItem->Enable(false);
		}

		if (favoritesMenu->GetMenuItemCount() > 0) {
			AppendSubMenu(favoritesMenu, "Favorites", "Add a favorite item to the container");
		} else {
			delete favoritesMenu;
		}
	}

	if (parentContainer && !parentContainer->getVector().empty()) {
		Append(CONTAINER_POPUP_MENU_REMOVE_ALL, "Remove &All Items", "Remove every item from this container");
	}

	if (btn->edit_item) {
		Append(CONTAINER_POPUP_MENU_REMOVE, "&Remove Item", "Remove this item from the container");

		const uint16_t itemId = btn->edit_item->getID();
		wxString toggleLabel = IsFavorite(itemId, buckets) ? "Remove from &Favorites" : "Add to &Favorites";
		Append(CONTAINER_POPUP_MENU_TOGGLE_FAVORITE, toggleLabel, "Toggle whether this item should be a favorite");
	}
}
