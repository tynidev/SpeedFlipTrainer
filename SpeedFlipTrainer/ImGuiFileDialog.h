#pragma once

#include "IMGUI/imgui.h"
#include "IMGUI/imgui_internal.h"
#include <chrono>
#include <string>
#include <filesystem>
#include <sstream>

using namespace std;
using namespace std::chrono_literals;
using namespace filesystem;

namespace ImGui {

	enum class FileDialogType {
		SelectFile,
		SelectFolder
	};
	enum class FileDialogSortOrder {
		Up,
		Down,
		None
	};

	class FileDialog {

	public:
		string name =  "Select File or Folder";
		bool open = false;
		filesystem::path workingDirectory;
		filesystem::path selected;

		bool ShowFileDialog(FileDialogType type = FileDialogType::SelectFile);

	private:
		FileDialogSortOrder fileSortOrder = FileDialogSortOrder::None;
		FileDialogSortOrder sizeSortOrder = FileDialogSortOrder::None;
		FileDialogSortOrder dateSortOrder = FileDialogSortOrder::None;
		FileDialogSortOrder typeSortOrder = FileDialogSortOrder::None;
		void SortFiles(vector<directory_entry>& files);
	};
}
