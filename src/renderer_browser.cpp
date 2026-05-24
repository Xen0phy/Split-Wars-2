// renderer_browser.cpp
#include "renderer_shared.h"

namespace fs = std::filesystem;

// Moves a route file (and its sibling .history) to destDir.
// Updates CurrentRouteFilepath/CurrentHistoryPath if the active route was moved.
static void MoveRouteFile(const std::string& srcJson, const std::string& destDir)
{
    try
    {
        fs::path src(srcJson);
        fs::path dstDir(destDir);
        fs::create_directories(dstDir);

        fs::path dstJson    = dstDir / src.filename();
        fs::path srcHistory = src; srcHistory.replace_extension(".history");
        fs::path dstHistory = dstDir / srcHistory.filename();

        fs::rename(src, dstJson);
        if (fs::exists(srcHistory))
            fs::rename(srcHistory, dstHistory);

        if (CurrentRouteFilepath == srcJson)
        {
            CurrentRouteFilepath = dstJson.string();
            CurrentHistoryPath   = dstHistory.string();
        }
    }
    catch (...) {}
}

// Recursive helper that renders a folder node using ImGui TreeNodes.
// Returns true if a route was selected (so the browser can auto-close).
// dragDropNeedsRefresh is set true when a file is moved via drag & drop.
static bool RenderFolderNode(const RouteFolder& folder, bool& dragDropNeedsRefresh)
{
    for (const auto& sub : folder.SubFolders)
    {
        bool open = ImGui::TreeNode(sub.FolderName.c_str());

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ROUTE_FILEPATH"))
            {
                std::string srcPath(static_cast<const char*>(payload->Data),
                                    payload->DataSize - 1);
                MoveRouteFile(srcPath, sub.FolderPath);
                dragDropNeedsRefresh = true;
            }
            ImGui::EndDragDropTarget();
        }

        if (open)
        {
            if (RenderFolderNode(sub, dragDropNeedsRefresh))
            {
                ImGui::TreePop();
                return true;
            }
            ImGui::TreePop();
        }
    }

    for (const auto& rf : folder.Routes)
    {
        bool clicked = ImGui::Selectable(rf.Name.c_str(), false,
                                         ImGuiSelectableFlags_AllowItemOverlap);

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("ROUTE_FILEPATH",
                rf.Filepath.c_str(),
                rf.Filepath.size() + 1);
            ImGui::Text("Move: %s", rf.Name.c_str());
            ImGui::EndDragDropSource();
        }

        if (clicked)
        {
            LoadRouteFile(rf);
            return true;
        }
    }

    return false;
}

void RenderRouteBrowserWindow()
{
    if (!ShowRouteBrowser) return;

    static RouteFolder routeTree;
    static bool        needsRefresh = true;

    if (needsRefresh)
    {
        routeTree    = BuildRouteTree(AddonDir);
        needsRefresh = false;
    }

    ImGui::SetNextWindowSize(ImVec2(320.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Routes", &ShowRouteBrowser);

    if (ImGui::Button("Refresh"))
        needsRefresh = true;

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Tip: drag a route onto a folder to move it.");
    ImGui::Spacing();

    bool dndRefresh = false;
    bool selected   = RenderFolderNode(routeTree, dndRefresh);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("[ root ]");
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ROUTE_FILEPATH"))
        {
            std::string srcPath(static_cast<const char*>(payload->Data),
                                payload->DataSize - 1);
            MoveRouteFile(srcPath, AddonDir);
            dndRefresh = true;
        }
        ImGui::EndDragDropTarget();
    }

    if (dndRefresh)
        needsRefresh = true;

    if (selected)
    {
        ShowRouteBrowser = false;
        needsRefresh     = true;
    }

    ImGui::End();
}
