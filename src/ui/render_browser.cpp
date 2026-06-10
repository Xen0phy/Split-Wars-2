// renderer_browser.cpp
// Implements the "Routes" browser window — the file picker that lets players
// navigate the addon's route folder, select a route to load, and reorganise
// routes by dragging them between sub-folders.
//
// The window builds a RouteFolder tree from disk once (and again whenever
// a refresh is requested), then renders it recursively with ImGui TreeNodes.
// Drag-and-drop between folder nodes moves the underlying .json (and its
// paired .history) file on disk and triggers a tree refresh.

#include "render_shared.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// MoveRouteFile  (file-private helper)
// ---------------------------------------------------------------------------
// Moves a route's .json file to destDir, and also moves the sibling .history
// file if one exists (history files share the same base name as the route).
//
// If the route being moved is the one currently loaded, the global path
// variables are updated to point at the new location so subsequent saves
// (e.g. after a run finishes) still land in the right place.
//
// All filesystem errors are silently swallowed — if the move fails the
// file simply stays where it was.
// ---------------------------------------------------------------------------
static void MoveRouteFile(const std::string& srcJson, const std::string& destDir)
{
    try
    {
        fs::path src(srcJson);
        fs::path dstDir(destDir);
        fs::create_directories(dstDir); // Create the destination folder if it doesn't exist yet

        fs::path dstJson    = dstDir / src.filename();
        fs::path srcHistory = src; srcHistory.replace_extension(".history");
        fs::path dstHistory = dstDir / srcHistory.filename();

        fs::rename(src, dstJson);
        if (fs::exists(srcHistory))
            fs::rename(srcHistory, dstHistory);

        // If this was the active route, update the global paths so future saves
        // continue to work correctly from the new location.
        if (CurrentRouteFilepath == srcJson)
        {
            CurrentRouteFilepath = dstJson.string();
            CurrentHistoryPath   = dstHistory.string();
        }
    }
    catch (...) {}
}

// ---------------------------------------------------------------------------
// DeleteRouteFile  (file-private helper)
// ---------------------------------------------------------------------------
// Permanently deletes a route's .json file and its sibling .history file
// from disk.  If the deleted route is currently loaded, the global path
// variables are cleared so future saves don't attempt to write to a
// non-existent file — the route data stays in memory so the user can
// save it again under a new name if needed.
//
// All filesystem errors are silently swallowed.
// ---------------------------------------------------------------------------
static void DeleteRouteFile(const std::string& jsonPath)
{
    try
    {
        fs::path src(jsonPath);
        fs::path history = src; history.replace_extension(".history");

        fs::remove(src);
        if (fs::exists(history))
            fs::remove(history);

        // If this was the active route, clear the paths so saves don't
        // try to write to the deleted location.  Route data stays in memory.
        if (CurrentRouteFilepath == jsonPath)
        {
            CurrentRouteFilepath.clear();
            CurrentHistoryPath.clear();
        }
    }
    catch (...) {}
}


// ---------------------------------------------------------------------------
// RenderFolderNode  (file-private recursive helper)
// ---------------------------------------------------------------------------
// Draws one level of the route tree using ImGui TreeNodes for sub-folders and
// ImGui Selectables for individual route files.  Calls itself recursively for
// nested sub-folders.
//
// Drag-and-drop:
//   • Each route Selectable is a drag source that carries the route's full
//     file path as the payload (type "ROUTE_FILEPATH").
//   • Each folder TreeNode is a drop target.  Dropping a route onto a folder
//     calls MoveRouteFile() and sets dragDropNeedsRefresh so the tree is
//     rebuilt next frame.
//
// Right-click on a route sets pendingDeletePath so the caller can show a
// confirmation popup before permanently deleting the file.
//
// Returns true when the user clicks a route, so the caller can schedule a
// tree refresh and stop traversing remaining nodes this frame.
// ---------------------------------------------------------------------------
static bool RenderFolderNode(const RouteFolder& folder, bool& dragDropNeedsRefresh,
                              std::string& pendingDeletePath)
{
    // --- Sub-folders ---
    for (const auto& sub : folder.SubFolders)
    {
        bool open = ImGui::TreeNode(sub.FolderName.c_str());

        // Make each folder a drag-and-drop target so routes can be moved into it.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ROUTE_FILEPATH"))
            {
                std::string srcPath(static_cast<const char*>(payload->Data),
                                    payload->DataSize - 1);
                MoveRouteFile(srcPath, sub.FolderPath);
                dragDropNeedsRefresh = true; // Tree is now stale — rebuild next frame
            }
            ImGui::EndDragDropTarget();
        }

        if (open)
        {
            // Recurse into the sub-folder; propagate the "selected" early-out upward.
            if (RenderFolderNode(sub, dragDropNeedsRefresh, pendingDeletePath))
            {
                ImGui::TreePop();
                return true;
            }
            ImGui::TreePop();
        }
    }

    // --- Route files inside this folder ---
    for (const auto& rf : folder.Routes)
    {
        bool clicked = ImGui::Selectable(rf.Name.c_str(), false,
                                         ImGuiSelectableFlags_AllowItemOverlap);

        // Make each route a drag source so it can be dropped onto a folder.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            // Payload is the full file path (null-terminated, so +1 for the '\0').
            ImGui::SetDragDropPayload("ROUTE_FILEPATH",
                rf.Filepath.c_str(),
                rf.Filepath.size() + 1);
            ImGui::Text("Move: %s", rf.Name.c_str()); // Tooltip shown while dragging
            ImGui::EndDragDropSource();
        }

        // Right-click — mark for deletion; confirmation popup shown by caller.
        char ctxId[64]; snprintf(ctxId, sizeof(ctxId), "##routectx_%s", rf.Name.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup(ctxId);
        if (ImGui::BeginPopup(ctxId))
        {
            if (ImGui::MenuItem("Delete Route"))
                pendingDeletePath = rf.Filepath;
            ImGui::EndPopup();
        }

        if (clicked)
        {
            LoadRouteFile(rf);
            return true; // Signal to caller: a route was selected
        }
    }

    return false;
}


// ---------------------------------------------------------------------------
// RenderRouteBrowserWindow
// ---------------------------------------------------------------------------
// Draws the "Routes" ImGui window.  The route tree is loaded once from disk
// (lazily on first draw) and cached in a static local.  A "Refresh" button
// and drag-and-drop moves both invalidate the cache so it is rebuilt on the
// next frame.
//
// The root of the addon directory is also a drop target (rendered below the
// tree as a "[ root ]" label) so routes can be moved back out of sub-folders.
//
// Selecting a route closes the browser window automatically and marks the
// tree dirty so it reflects any external changes next time it is opened.
// ---------------------------------------------------------------------------
void RenderRouteBrowserWindow()
{
    if (!ShowRouteBrowser) return;

    static RouteFolder routeTree;
    static bool        needsRefresh = true; // True on first draw to force an initial load

    if (needsRefresh)
    {
        routeTree    = BuildRouteTree(AddonDir); // Scan the addon directory and build the tree
        needsRefresh = false;
    }

    // Window size
    static bool firstFrame = true;
    if (firstFrame) {
        ImGui::SetNextWindowSize(ImVec2(BrowserWindowW, BrowserWindowH), ImGuiCond_Always);
        firstFrame = false;
    }
    ImGui::Begin("Split Wars 2 - Route Browser", &ShowRouteBrowser);
    ImVec2 sz = ImGui::GetWindowSize();
    BrowserWindowW = sz.x;
    BrowserWindowH = sz.y;

    if (ImGui::Button("Refresh"))
        needsRefresh = true;

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Tip: drag a route onto a folder to move it.");
    ImGui::Spacing();

    bool dndRefresh = false; // Set by RenderFolderNode if a drag-and-drop move occurred
    static std::string pendingDeletePath; // Set by RenderFolderNode on right-click delete
    bool selected   = RenderFolderNode(routeTree, dndRefresh, pendingDeletePath);

    // --- Confirmation popup for route deletion ---
    // Opened when pendingDeletePath is set by RenderFolderNode. Shown here
    // rather than inside the recursive function so only one popup exists at
    // a time regardless of tree depth.
    if (!pendingDeletePath.empty())
        ImGui::OpenPopup("##confirmdelete");

    if (ImGui::BeginPopup("##confirmdelete"))
    {
        ImGui::Text("Permanently delete this route and its history?");
        ImGui::TextDisabled("%s", fs::path(pendingDeletePath).filename().string().c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Yes, delete"))
        {
            DeleteRouteFile(pendingDeletePath);
            pendingDeletePath.clear();
            needsRefresh = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            pendingDeletePath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- Root drop target ---
    // Rendered at the bottom of the list so the player can move a route back to
    // the top-level addon directory by dropping it here.
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

    // Propagate the drag-and-drop refresh request.
    if (dndRefresh)
        needsRefresh = true;

    // Schedule a refresh when the user selects a route so the browser
    // reflects any external changes next time the tree is rendered.
    if (selected)
        needsRefresh = true;

    ImGui::End();
}