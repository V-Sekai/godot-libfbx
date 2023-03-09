// © Copyright 2014-2022, Juan Linietsky, Ariel Manzur and the Godot community (CC-BY 3.0)
#include "libfbx2gltf.h"

#include <godot_cpp/classes/editor_file_system_import_format_support_query.hpp>
#include <godot_cpp/classes/editor_scene_format_importer.hpp>
#include <godot_cpp/classes/gltf_document.hpp>
#include <godot_cpp/classes/gltf_state.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource_importer.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include "FBX2glTF.h"
#include "fbx/Fbx2Raw.hpp"
#include "gltf/Raw2Gltf.hpp"
#include "utils/File_Utils.hpp"
#include "utils/String_Utils.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <unordered_map>
#include <vector>

using namespace godot;

bool verboseOutput = false;

Variant EditorSceneFormatImporterFBX2GLTF::_get_option_visibility(
    const String& p_path,
    bool p_for_animation,
    const String& p_option) const {
  return true;
}

void EditorSceneFormatImporterFBX2GLTF::_get_import_options(const String& p_path) {}

Object* EditorSceneFormatImporterFBX2GLTF::_import_scene(
    const String& p_path,
    uint32_t p_flags,
    const Dictionary& p_options) {
  // Get global paths for source and sink.

  const String source_global = ProjectSettings::get_singleton()->globalize_path(p_path);
  const String sink = String(".godot/imported/") + p_path.get_file().get_basename() + String("-") +
      p_path.md5_text() + String(".glb");
  const String sink_global = ProjectSettings::get_singleton()->globalize_path(sink);

  GltfOptions gltfOptions;

  gltfOptions.usePBRMetRough = true;
  gltfOptions.outputBinary = true;

  std::string inputPath = source_global.utf8().get_data();

  std::string outputPath = sink_global.utf8().get_data();

  // the output folder in .gltf mode, not used for .glb
  std::string outputFolder;

  // the path of the actual .glb or .gltf file
  std::string modelPath;
  const auto& suffix = FileUtils::GetFileSuffix(outputPath);

  // Assume binary output if extension is glb
  if (suffix.has_value() && suffix.value() == "glb") {
    gltfOptions.outputBinary = true;
  }

  if (gltfOptions.outputBinary) {
    // add .glb to output path, unless it already ends in exactly that
    outputFolder = FileUtils::getFolder(outputPath) + "/";
    if (suffix.has_value() && suffix.value() == "glb") {
      modelPath = outputPath;
    } else {
      modelPath = outputPath + ".glb";
    }
    // if the extension is gltf set the output folder to the parent directory
  } else if (suffix.has_value() && suffix.value() == "gltf") {
    outputFolder = FileUtils::getFolder(outputPath) + "/";
    modelPath = outputPath;
  } else {
    // in gltf mode, we create a folder and write into that
    outputFolder = fmt::format("{}_out/", outputPath.c_str());
    modelPath = outputFolder + FileUtils::GetFileName(outputPath) + ".gltf";
  }
  if (!FileUtils::CreatePath(modelPath.c_str())) {
    fmt::fprintf(stderr, "ERROR: Failed to create folder: %s'\n", outputFolder.c_str());
    return nullptr;
  }

  ModelData* data_render_model = nullptr;
  RawModel raw;

  if (verboseOutput) {
    fmt::printf("Loading FBX File: %s\n", inputPath);
  }
  if (!LoadFBXFile(raw, inputPath, {"png", "jpg", "jpeg"}, gltfOptions)) {
    fmt::fprintf(stderr, "ERROR:: Failed to parse FBX: %s\n", inputPath);
    return nullptr;
  }

  raw.Condense(gltfOptions.maxSkinningWeights, gltfOptions.normalizeSkinningWeights);
  raw.TransformGeometry(gltfOptions.computeNormals);

  std::ofstream outStream; // note: auto-flushes in destructor
  const auto streamStart = outStream.tellp();

  outStream.open(modelPath, std::ios::trunc | std::ios::ate | std::ios::out | std::ios::binary);
  if (outStream.fail()) {
    fmt::fprintf(stderr, "ERROR:: Couldn't open file for writing: %s\n", modelPath.c_str());
    return nullptr;
  }
  data_render_model = Raw2Gltf(outStream, outputFolder, raw, gltfOptions);

  if (gltfOptions.outputBinary) {
    fmt::printf(
        "Wrote %lu bytes of binary glTF to %s.\n",
        (unsigned long)(outStream.tellp() - streamStart),
        modelPath);
    delete data_render_model;
  } else {
    delete data_render_model;
    return nullptr;
  }

  // Import the generated glTF.

  // Use GLTFDocument instead of glTF importer to keep image references.
  Ref<GLTFDocument> gltf;
  gltf.instantiate();
  Ref<GLTFState> state;
  state.instantiate();
  Error err = gltf->append_from_file(sink, state, p_flags, p_path.get_base_dir());
  if (err != OK) {
    return nullptr;
  }

  return gltf->generate_scene(
      state,
      (float)p_options["animation/fps"],
      (bool)p_options["animation/trimming"],
      (bool)p_options["animation/remove_immutable_tracks"]);
}

PackedStringArray EditorSceneFormatImporterFBX2GLTF::_get_extensions() const {
  PackedStringArray extensions;
  extensions.push_back("fbx");
  return extensions;
}

uint32_t EditorSceneFormatImporterFBX2GLTF::_get_import_flags() const {
  return EditorSceneFormatImporter::IMPORT_SCENE | EditorSceneFormatImporter::IMPORT_ANIMATION;
}
