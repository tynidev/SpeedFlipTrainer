#include "pch.h"
#include "ImGuiFileDialog.h"

bool ImGui::FileDialog::ShowFileDialog(FileDialogType type)
{
	if (!open)
		return false;

	ImGui::SetNextWindowSize(ImVec2(740.0f, 410.0f));
	ImGui::Begin(name.c_str(), nullptr, ImGuiWindowFlags_NoResize);

	vector<directory_entry> files;
	vector<directory_entry> folders;
	try {
		for (auto& p : directory_iterator(workingDirectory)) {
			if (p.is_directory()) {
				folders.push_back(p);
			}
			else {
				files.push_back(p);
			}
		}
	}
	catch (...) {}

	ImGui::Text("%s", workingDirectory.string().c_str());

	ImGui::BeginChild("Directories##1", ImVec2(200, 300), true, ImGuiWindowFlags_HorizontalScrollbar);

	// List directories
	if (ImGui::Selectable("..", false, ImGuiSelectableFlags_None, ImVec2(ImGui::GetWindowContentRegionWidth(), 0))) {
		workingDirectory = workingDirectory.parent_path();
		if (type == FileDialogType::SelectFolder)
			selected = workingDirectory;
	}
	for (int i = 0; i < folders.size(); ++i) {
		if (ImGui::Selectable(folders[i].path().stem().string().c_str(), folders[i].path() == workingDirectory, ImGuiSelectableFlags_None, ImVec2(ImGui::GetWindowContentRegionWidth(), 0))) {
			workingDirectory = folders[i].path();
			ImGui::SetScrollHereY(0.0f);
			if (type == FileDialogType::SelectFolder)
				selected = workingDirectory;
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("Files##1", ImVec2(516, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::Columns(4);
	static float initialSpacingColumn0 = 230.0f;
	if (initialSpacingColumn0 > 0) {
		ImGui::SetColumnWidth(0, initialSpacingColumn0);
		initialSpacingColumn0 = 0.0f;
	}
	static float initialSpacingColumn1 = 80.0f;
	if (initialSpacingColumn1 > 0) {
		ImGui::SetColumnWidth(1, initialSpacingColumn1);
		initialSpacingColumn1 = 0.0f;
	}
	static float initialSpacingColumn2 = 80.0f;
	if (initialSpacingColumn2 > 0) {
		ImGui::SetColumnWidth(2, initialSpacingColumn2);
		initialSpacingColumn2 = 0.0f;
	}
	if (ImGui::Selectable("File")) {
		sizeSortOrder = FileDialogSortOrder::None;
		dateSortOrder = FileDialogSortOrder::None;
		typeSortOrder = FileDialogSortOrder::None;
		fileSortOrder = (fileSortOrder == FileDialogSortOrder::Down ? FileDialogSortOrder::Up : FileDialogSortOrder::Down);
	}
	ImGui::NextColumn();
	if (ImGui::Selectable("Size")) {
		fileSortOrder = FileDialogSortOrder::None;
		dateSortOrder = FileDialogSortOrder::None;
		typeSortOrder = FileDialogSortOrder::None;
		sizeSortOrder = (sizeSortOrder == FileDialogSortOrder::Down ? FileDialogSortOrder::Up : FileDialogSortOrder::Down);
	}
	ImGui::NextColumn();
	if (ImGui::Selectable("Type")) {
		fileSortOrder = FileDialogSortOrder::None;
		dateSortOrder = FileDialogSortOrder::None;
		sizeSortOrder = FileDialogSortOrder::None;
		typeSortOrder = (typeSortOrder == FileDialogSortOrder::Down ? FileDialogSortOrder::Up : FileDialogSortOrder::Down);
	}
	ImGui::NextColumn();
	if (ImGui::Selectable("Date")) {
		fileSortOrder = FileDialogSortOrder::None;
		sizeSortOrder = FileDialogSortOrder::None;
		typeSortOrder = FileDialogSortOrder::None;
		dateSortOrder = (dateSortOrder == FileDialogSortOrder::Down ? FileDialogSortOrder::Up : FileDialogSortOrder::Down);
	}
	ImGui::NextColumn();
	ImGui::Separator();

	// Sort files
	SortFiles(files);

	// List files
	for (int i = 0; i < files.size(); ++i) {
		auto entry = files[i];
		auto file = entry.path();

		// File Name
		if (ImGui::Selectable(file.filename().string().c_str(), selected == file, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(ImGui::GetWindowContentRegionWidth(), 0))) {
			if (type == FileDialogType::SelectFile)
				selected = file;
		}

		// File size
		ImGui::NextColumn();
		ImGui::TextUnformatted(to_string(entry.file_size()).c_str());

		// File Type
		ImGui::NextColumn();
		ImGui::TextUnformatted(file.extension().string().c_str());

		// File date
		ImGui::NextColumn();
		auto ftime = entry.last_write_time();
		auto st = chrono::time_point_cast<chrono::system_clock::duration>(ftime - decltype(ftime)::clock::now() + chrono::system_clock::now());
		time_t tt = chrono::system_clock::to_time_t(st);
		tm* mt = localtime(&tt);
		stringstream ss;
		ss << put_time(mt, "%F %R");
		ImGui::TextUnformatted(ss.str().c_str());
		ImGui::NextColumn();
	}
	ImGui::EndChild();

	// Selected file text box
	string selectedFilePath = selected.string();
	char* buf = &selectedFilePath[0];
	ImGui::PushItemWidth(724);
	ImGui::InputText("", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 120);

	bool ret = false;
	if (ImGui::Button("Cancel")) {
		open = false;
	}

	ImGui::SameLine();
	if (ImGui::Button("Choose")) {
		if (type == FileDialogType::SelectFolder) {
			if (selected.extension() != "") {
				ImGui::TextColored(ImColor(1.0f, 0.0f, 0.2f, 1.0f), "Error: You must select a folder!");
			}
			else {
				selected = workingDirectory;
				open = false;
				ret = true;
			}
		}
		else
		{
			open = false;
			ret = true;
		}
	}

	ImGui::End();
	return ret;
}

void ImGui::FileDialog::SortFiles(vector<directory_entry>& files)
{
	if (fileSortOrder != FileDialogSortOrder::None) {
		if (fileSortOrder == FileDialogSortOrder::Down) {
			sort(files.begin(), files.end(), [](const directory_entry& a, const directory_entry& b) {
				return a.path().filename().string() > b.path().filename().string();
			});
		}
		else
		{
			sort(files.begin(), files.end(), [](const directory_entry& a, const directory_entry& b) {
				return a.path().filename().string() < b.path().filename().string();
			});
		}
	}
	else if (sizeSortOrder != FileDialogSortOrder::None) {
		if (sizeSortOrder == FileDialogSortOrder::Down) {
			sort(files.begin(), files.end(), [](const directory_entry& a, const directory_entry& b) {
				return a.file_size() > b.file_size();
			});
		}
		else {
			sort(files.begin(), files.end(), [](const directory_entry& a, const directory_entry& b) {
				return a.file_size() < b.file_size();
			});
		}
	}
	else if (typeSortOrder != FileDialogSortOrder::None) {
		if (typeSortOrder == FileDialogSortOrder::Down) {
			sort(files.begin(), files.end(), [](const directory_entry& a, const directory_entry& b) {

				return a.path().extension().string() > b.path().extension().string();
			});
		}
		else {
			sort(files.begin(), files.end(), [](const directory_entry& a, const directory_entry& b) {

				return a.path().extension().string() < b.path().extension().string();
			});
		}
	}
	else if (dateSortOrder != FileDialogSortOrder::None) {
		if (dateSortOrder == FileDialogSortOrder::Down) {
			sort(files.begin(), files.end(), [](const directory_entry& a, const directory_entry& b) {
				return a.last_write_time() > b.last_write_time();
			});
		}
		else {
			sort(files.begin(), files.end(), [](const directory_entry& a, const directory_entry& b) {
				return a.last_write_time() < b.last_write_time();
			});
		}
	}
}