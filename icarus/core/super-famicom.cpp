auto Icarus::superFamicomManifest(string location) -> string {
  vector<uint8> buffer;
  auto files = directory::files(location, "*.rom");
  concatenate(buffer, {location, "program.rom"});
  concatenate(buffer, {location, "data.rom"   });
  for(auto& file : files.match("slot-*.rom"   )) concatenate(buffer, {location, file});
  for(auto& file : files.match("*.boot.rom"   )) concatenate(buffer, {location, file});
  for(auto& file : files.match("*.program.rom")) concatenate(buffer, {location, file});
  for(auto& file : files.match("*.data.rom"   )) concatenate(buffer, {location, file});
  return superFamicomManifest(buffer, location);
}

auto Icarus::superFamicomManifest(vector<uint8>& buffer, string location, bool* firmwareAppended) -> string {
  string markup;
  string digest = Hash::SHA256(buffer.data(), buffer.size()).digest();

  if(settings["icarus/UseDatabase"].boolean() && !markup) {
    for(auto node : database.superFamicom) {
      if(node["sha256"].text() == digest) {
        markup.append(node.text(), "\n  sha256:   ", digest, "\n");
        break;
      }
    }
  }

  if(settings["icarus/UseHeuristics"].boolean() && !markup) {
    bool hasMSU1 = file::exists({location, "msu1.rom"});
    SuperFamicomCartridge cartridge{buffer.data(), buffer.size(), hasMSU1};
    if(markup = cartridge.markup) {
      if(firmwareAppended) *firmwareAppended = cartridge.firmware_appended;
      markup.append("\n");
      markup.append("information\n");
      markup.append("  region: ", cartridge.region == SuperFamicomCartridge::Region::NTSC ? "NTSC" : "PAL", "\n");
      markup.append("  title:  ", prefixname(location), "\n");
      markup.append("  sha256: ", digest, "\n");
      markup.append("  note:   ", "heuristically generated by icarus\n");
    }
  }

  return markup;
}

auto Icarus::superFamicomManifestScan(vector<Markup::Node>& roms, Markup::Node node) -> void {
  if(node["name"].text().endsWith(".rom")) roms.append(node);
  for(auto leaf : node) superFamicomManifestScan(roms, leaf);
}

auto Icarus::superFamicomImport(vector<uint8>& buffer, string location) -> string {
  auto name = prefixname(location);
  auto source = pathname(location);
  string target{settings["Library/Location"].text(), "Super Famicom/", name, ".sfc/"};
//if(directory::exists(target)) return failure("game already exists");

  bool firmwareAppended = true;
  auto markup = superFamicomManifest(buffer, location, &firmwareAppended);
  if(!markup) return failure("failed to parse ROM image");

  auto document = BML::unserialize(markup);
  vector<Markup::Node> roms;
  superFamicomManifestScan(roms, document["board"]);
  for(auto rom : roms) {
    auto name = rom["name"].text();
    auto size = rom["size"].natural();
    if(name == "program.rom" || name == "data.rom" || firmwareAppended) continue;
    if(file::size({source, name}) != size) return failure({"firmware (", name, ") missing or invalid"});
  }

  if(!directory::create(target)) return failure("library path unwritable");
  if(file::exists({source, name, ".srm"}) && !file::exists({target, "save.ram"})) {
    file::copy({source, name, ".srm"}, {target, "save.ram"});
  }

  if(settings["icarus/CreateManifests"].boolean()) file::write({target, "manifest.bml"}, markup);
  uint offset = (buffer.size() & 0x7fff) == 512 ? 512 : 0;  //skip header if present
  for(auto rom : roms) {
    auto name = rom["name"].text();
    auto size = rom["size"].natural();
    if(name == "program.rom" || name == "data.rom" || firmwareAppended) {
      if(size > buffer.size() - offset) return failure("ROM image is missing data");
      file::write({target, name}, buffer.data() + offset, size);
      offset += size;
    } else {
      auto firmware = file::read({source, name});
      file::write({target, name}, firmware);
    }
  }
  return success(target);
}
