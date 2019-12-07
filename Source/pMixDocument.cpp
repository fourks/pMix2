/*
  ==============================================================================

    pMixDocument.cpp
    Author:  Oliver Larkin
 
  ==============================================================================
*/

#include "pMixDocument.h"
#include "pMixAudioEngine.h"
#include "pMixConstants.h"

const int PMixDocument::midiChannelNumber = 0x1000;

PMixDocument::PMixDocument (PMixAudioEngine& audioEngine)
: FileBasedDocument (filenameSuffix, filenameWildcard, TRANS("Load a pMix patch"), TRANS("Save a pMix patch"))
, audioEngine(audioEngine)
, lastUID (0)
, lastPresetUID(0)
, snapGridPixels (8)
, snapActive (true)
, snapShown (true)
, componentOverlayOpacity (0.33f)
, drawPath(FAUST_DRAW_PATH)
{
  startTimer (20);
}

PMixDocument::~PMixDocument()
{
}

uint32 PMixDocument::getNextUID() noexcept
{
  return ++lastUID;
}

int PMixDocument::getNextPresetUID() noexcept
{
  return ++lastPresetUID;
}

int PMixDocument::getNumNodes() const noexcept
{
  return audioEngine.getGraph().getNumNodes();
}

const AudioProcessorGraph::Node::Ptr PMixDocument::getNode (const int index) const noexcept
{
  return audioEngine.getGraph().getNode (index);
}

const AudioProcessorGraph::Node::Ptr PMixDocument::getNodeForId (NodeID nodeIDs) const noexcept
{
  return audioEngine.getGraph().getNodeForId (nodeIDs);
}

NodeID PMixDocument::addNode (const PluginDescription* desc, double x, double y)
{
  AudioProcessorGraph::Node* node = nullptr;

  if (desc != nullptr)
  {
    String errorMessage;
    
    std::unique_ptr<AudioPluginInstance> instance = audioEngine.createPluginInstance(*desc, errorMessage);

    jassert(instance != nullptr);
    
    node = audioEngine.getGraph().addNode (std::move (instance));

    FaustAudioPluginInstance* faustProc = dynamic_cast<FaustAudioPluginInstance*>(instance.get());
    
    if (faustProc)
    {
      faustProc->initialize(getLibraryPath(), drawPath);
    }

    if (node != nullptr)
    {
      node->properties.set ("x", x);
      node->properties.set ("y", y);
      node->properties.set ("uiLastX", Random::getSystemRandom().nextInt (500));
      node->properties.set ("uiLastY", Random::getSystemRandom().nextInt (500));
      node->properties.set ("uiStatus", kUIStatusEmbed);
      
      if (!InternalPluginFormat::isInternalFormat(desc->name))
      {
        node->properties.set ("colour", defaultColours.getNextColour().toString());
        node->properties.set ("iposx", x);
        node->properties.set ("iposy", y);
        node->properties.set ("update", false);
        Array<var> presets;
        node->properties.set("presets", presets);
        Array<var> params;
        node->properties.set("params", params);
      }

      changed();
    }
    else
    {
      AlertWindow::showMessageBox (AlertWindow::WarningIcon,
                                   TRANS("Couldn't create node"),
                                   errorMessage);
      
      return NodeID(0xFFFFFFFF);
    }
  }
  
  return node->nodeID;
}

void PMixDocument::removeNode (NodeID nodeID)
{
  if (audioEngine.getGraph().removeNode (nodeID))
    changed();
}

void PMixDocument::disconnectNode (NodeID nodeID)
{
  if (audioEngine.getGraph().disconnectNode (nodeID))
    changed();
}

void PMixDocument::removeIllegalConnections()
{
  if (audioEngine.getGraph().removeIllegalConnections())
    changed();
}

void PMixDocument::setNodeUIStatus(NodeID nodeID, const uint32 uiStatus)
{
  const AudioProcessorGraph::Node::Ptr n (audioEngine.getGraph().getNodeForId (nodeID));
  
  if (n != nullptr)
  {
    n->properties.set ("uiStatus", jlimit<int>(0, 2, uiStatus));
  }
}

void PMixDocument::setNodePosition (NodeID nodeID, double x, double y)
{
  const AudioProcessorGraph::Node::Ptr n (audioEngine.getGraph().getNodeForId (nodeID));

  if (n != nullptr)
  {
    n->properties.set ("x", jlimit (0.0, 1.0, x));
    n->properties.set ("y", jlimit (0.0, 1.0, y));
  }
}

void PMixDocument::getNodePosition (NodeID nodeID, double& x, double& y) const
{
  x = y = 0;

  const AudioProcessorGraph::Node::Ptr n (audioEngine.getGraph().getNodeForId (nodeID));

  if (n != nullptr)
  {
    x = (double) n->properties ["x"];
    y = (double) n->properties ["y"];
  }
}

int PMixDocument::getNumConnections() const noexcept
{
    return audioEngine.getGraph().getConnections().size();
}

const AudioProcessorGraph::Connection PMixDocument::getConnection (const int index) const noexcept
{
    AudioProcessorGraph::Connection connection = audioEngine.getGraph().getConnections()[index];
    return connection;
}

bool PMixDocument::isConnected (NodeID sourceNodeId, int sourceNodeChannel, NodeID destNodeId, int destNodeChannel) const noexcept
{
    AudioProcessorGraph::NodeAndChannel source {sourceNodeId, sourceNodeChannel};
    AudioProcessorGraph::NodeAndChannel destination {destNodeId, destNodeChannel};
    AudioProcessorGraph::Connection connection {source, destination};
    return audioEngine.getGraph().isConnected(connection);
}

bool PMixDocument::canConnect (NodeID sourceNodeId, int sourceNodeChannel, NodeID destNodeId, int destNodeChannel) const noexcept
{
    AudioProcessorGraph::NodeAndChannel source {sourceNodeId, sourceNodeChannel};
    AudioProcessorGraph::NodeAndChannel destination {destNodeId, destNodeChannel};
    AudioProcessorGraph::Connection connection {source, destination};
    return audioEngine.getGraph().canConnect(connection);
}

bool PMixDocument::addConnection (NodeID sourceNodeId, int sourceNodeChannel, NodeID destNodeId, int destNodeChannel)
{
    AudioProcessorGraph::NodeAndChannel source {sourceNodeId, sourceNodeChannel};
    AudioProcessorGraph::NodeAndChannel destination {destNodeId, destNodeChannel};
    AudioProcessorGraph::Connection connection {source, destination};
    const bool result = audioEngine.getGraph().addConnection (connection);

    if (result)
        changed();

    return result;
}

void PMixDocument::removeConnection (const int index)
{
    audioEngine.getGraph().removeConnection (this->getConnection(index));
    changed();
}

void PMixDocument::removeConnection (NodeID sourceNodeId, int sourceNodeChannel, NodeID destNodeId, int destNodeChannel)
{
    AudioProcessorGraph::NodeAndChannel source {sourceNodeId, sourceNodeChannel};
    AudioProcessorGraph::NodeAndChannel destination {destNodeId, destNodeChannel};
    AudioProcessorGraph::Connection connection {source, destination};

    if (audioEngine.getGraph().removeConnection (connection))
        changed();
}

void PMixDocument::clear()
{
  audioEngine.getGraph().clear();
  changed();
}

String PMixDocument::getDocumentTitle()
{
  if (! getFile().exists())
    return "Unnamed";

  return getFile().getFileNameWithoutExtension();
}

Result PMixDocument::loadDocument (const File& file)
{
  XmlDocument doc (file);
  std::unique_ptr<XmlElement> xml (doc.getDocumentElement());

  if (xml == nullptr || ! xml->hasTagName ("PMIXDOC"))
    return Result::fail ("Not a valid pMix file");

  restoreFromXml (*xml);
  return Result::ok();
}

Result PMixDocument::saveDocument (const File& file)
{
  std::unique_ptr<XmlElement> xml = createXml();

  if (! xml->writeToFile (file, String()))
    return Result::fail ("Couldn't write to the file");

  return Result::ok();
}

File PMixDocument::getLastDocumentOpened()
{
  RecentlyOpenedFilesList recentFiles;
  recentFiles.restoreFromString (audioEngine.getAppProperties().getUserSettings()->getValue ("recentPMixDocumentFiles"));

  return recentFiles.getFile (0);
}

void PMixDocument::setLastDocumentOpened (const File& file)
{
  RecentlyOpenedFilesList recentFiles;
  recentFiles.restoreFromString (audioEngine.getAppProperties().getUserSettings()->getValue ("recentPMixDocumentFiles"));

  recentFiles.addFile (file);

  audioEngine.getAppProperties().getUserSettings()
  ->setValue ("recentPMixDocumentFiles", recentFiles.toString());
}

 XmlElement* PMixDocument::createNodeXml (AudioProcessorGraph::Node* const node) noexcept
{
  AudioPluginInstance* plugin = dynamic_cast <AudioPluginInstance*> (node->getProcessor());

  if (plugin == nullptr)
  {
    jassertfalse;
    return nullptr;
  }

  XmlElement* e = new XmlElement ("NODE");
  e->setAttribute ("uid", (int) node->nodeID.uid);
  e->setAttribute ("x", node->properties ["x"].toString());
  e->setAttribute ("y", node->properties ["y"].toString());
  e->setAttribute ("uiLastX", node->properties ["uiLastX"].toString());
  e->setAttribute ("uiLastY", node->properties ["uiLastY"].toString());
  e->setAttribute ("uiStatus", node->properties ["uiStatus"].toString());
  
  PluginDescription pd;
  plugin->fillInPluginDescription (pd);
  
  if(!InternalPluginFormat::isInternalFormat(pd.name))
  {
    e->setAttribute("colour", node->properties ["colour"].toString());
    e->setAttribute ("iposx", node->properties ["iposx"].toString());
    e->setAttribute ("iposy", node->properties ["iposy"].toString());
  }
  
  e->addChildElement (pd.createXml().release());

  XmlElement* state = new XmlElement ("STATE");

  MemoryBlock m;
  node->getProcessor()->getStateInformation (m);
  state->addTextElement (m.toBase64Encoding());
  e->addChildElement (state);
  
  if(!InternalPluginFormat::isInternalFormat(pd.name))
  {
    XmlElement* params = new XmlElement ("PARAMS");
    Array<var>* paramsArray = node->properties.getVarPointer("params")->getArray();
    
    params->addTextElement("[");
    for(int i=0;i<paramsArray->size();i++)
    {
      var parameterIdx = paramsArray->getReference(i);
      
      params->addTextElement(parameterIdx.toString());
      
      if(i != paramsArray->size()-1)
        params->addTextElement(", ");
    }
    params->addTextElement("]");
    
    e->addChildElement(params);
        
    Array<var>* presetsArr = node->properties.getVarPointer("presets")->getArray();
    
    for(int i=0;i<presetsArr->size();i++)
    {
      XmlElement* presetXML = new XmlElement ("PRESET");
      DynamicObject* thePreset = presetsArr->getReference(i).getDynamicObject();
      presetXML->setAttribute("name", thePreset->getProperty("name").toString());
      presetXML->setAttribute("x", thePreset->getProperty("x").toString());
      presetXML->setAttribute("y", thePreset->getProperty("y").toString());
      presetXML->setAttribute("radius", thePreset->getProperty("radius").toString());
      presetXML->setAttribute("hidden", thePreset->getProperty("hidden").toString());
      //presetXML->setAttribute("distance", thePreset->getProperty("distance").toString());
      presetXML->setAttribute("coeff", thePreset->getProperty("coeff").toString());
      presetXML->setAttribute("uid", thePreset->getProperty("uid").toString());

      Array<var>* paramsArray = thePreset->getProperty("state").getArray();
      
      presetXML->addTextElement("[");
      for(int i=0;i<paramsArray->size();i++)
      {
        var parameterIdx = paramsArray->getReference(i);
        
        presetXML->addTextElement(parameterIdx.toString());
        
        if(i != paramsArray->size()-1)
          presetXML->addTextElement(", ");
      }
      
      presetXML->addTextElement("]");
      
      e->addChildElement(presetXML);
    }
  }
  
  return e;
}

void PMixDocument::createNodeFromXml (XmlElement& xml, const String& newSourceCode)
{
  PluginDescription pd;
  
  forEachXmlChildElement (xml, e)
  {
    if (pd.loadFromXml (*e))
      break;
  }
  
  String errorMessage;
  
  std::unique_ptr<AudioPluginInstance> instance = audioEngine.createPluginInstance(pd, errorMessage);
  
  jassert(instance != nullptr);
  
  if (pd.pluginFormatName == "FAUST")
  {
    FaustAudioPluginInstance* faustProc = dynamic_cast<FaustAudioPluginInstance*>(instance.get());
    faustProc->initialize(getLibraryPath(), drawPath);
    
    if (newSourceCode.length())
      faustProc->setSourceCode(newSourceCode, true);
    
    // TODO: this is a bit wrong!
    faustProc->prepareToPlay(44100., 8192);
    
//    xml.setAttribute("numInputs", faustProc->getNumInputChannels());
//    xml.setAttribute("numOutputs", faustProc->getNumOutputChannels()); ???
  }
  
  AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().addNode (std::move(instance), NodeID(xml.getIntAttribute ("uid"))));
  
  if (!newSourceCode.length())
  {
    if (const XmlElement* const state = xml.getChildByName ("STATE"))
    {
      MemoryBlock m;
      m.fromBase64Encoding (state->getAllSubText());
      
      node->getProcessor()->setStateInformation (m.getData(), (int) m.getSize());
    }
  }
  
  node->properties.set ("x", xml.getDoubleAttribute ("x"));
  node->properties.set ("y", xml.getDoubleAttribute ("y"));
  node->properties.set ("uiLastX", xml.getIntAttribute ("uiLastX"));
  node->properties.set ("uiLastY", xml.getIntAttribute ("uiLastY"));
  node->properties.set ("uiStatus", xml.getIntAttribute ("uiStatus"));

  // presets etc for faust & plugin nodes
  if(!InternalPluginFormat::isInternalFormat(pd.name))
  {
    node->properties.set ("colour", xml.getStringAttribute ("colour"));
    node->properties.set ("iposx", xml.getDoubleAttribute ("iposx"));
    node->properties.set ("iposy", xml.getDoubleAttribute ("iposy"));
    if (const XmlElement* const params = xml.getChildByName ("PARAMS"))
    {
      var vparams = JSON::parse(params->getAllSubText());
      node->properties.set ("params", vparams);
    }
    
    Array<var> presetsArr;
    
    forEachXmlChildElement (xml, e)
    {
      if (e->hasTagName ("PRESET"))
      {
        DynamicObject* obj = new DynamicObject();
        obj->setProperty("name", e->getStringAttribute("name"));
        obj->setProperty("x", e->getDoubleAttribute("x"));
        obj->setProperty("y", e->getDoubleAttribute("y"));
        obj->setProperty("radius", e->getDoubleAttribute("radius"));
        obj->setProperty("hidden", e->getBoolAttribute("hidden"));
        //  obj->setProperty("distance", e->getDoubleAttribute("distance"));
        obj->setProperty("coeff", e->getDoubleAttribute("coeff"));
        
        var vparams = JSON::parse(e->getAllSubText());
        obj->setProperty("state", vparams);
        obj->setProperty("uid", e->getIntAttribute("uid"));
        
        var preset = var(obj);
        presetsArr.add(preset);
      }
    }
    
    node->properties.set("presets", presetsArr);
  }
  
  changed();
}


std::unique_ptr<XmlElement> PMixDocument::createXml() const
{
  auto xml = std::make_unique<XmlElement> ("PMIXDOC");

  for (auto* node : audioEngine.getGraph().getNodes())
    xml->addChildElement(createNodeXml(node));

  for (auto& connection : audioEngine.getGraph().getConnections())
  {
    auto e = xml->createNewChildElement ("CONNECTION");

    e->setAttribute ("srcNode", (int) connection.source.nodeID.uid);
    e->setAttribute ("srcChannel", connection.source.channelIndex);
    e->setAttribute ("dstNode", (int) connection.destination.nodeID.uid);
    e->setAttribute ("dstChannel", connection.destination.channelIndex);
  }
  
//  XmlElement* e = new XmlElement ("MISC");
//
//  e->setAttribute ("snapPixels", snapGridPixels);
//  e->setAttribute ("snapActive", snapActive);
//  e->setAttribute ("snapShown", snapShown);
//  e->setAttribute ("overlayOpacity", String (componentOverlayOpacity, 3));
//  xml->addChildElement (e);

  return xml;
}

void PMixDocument::restoreFromXml (const XmlElement& xml)
{
  clear();

  forEachXmlChildElementWithTagName (xml, e, "NODE")
  {
    createNodeFromXml (*e);
    changed();
  }

  // without this here, dynamic IO audio processors don't get set up and the connections fail
  audioEngine.getGraph().prepareToPlay(44100., 1024);

  forEachXmlChildElementWithTagName (xml, e, "CONNECTION")
  {
    addConnection (NodeID (e->getIntAttribute ("srcNode")),
                            e->getIntAttribute ("srcChannel"),
                   NodeID (e->getIntAttribute ("dstNode")),
                            e->getIntAttribute ("dstChannel"));
  }

  audioEngine.getGraph().removeIllegalConnections();
}

bool PMixDocument::isSnapActive (const bool disableIfCtrlKeyDown) const noexcept
{
  return snapActive != (disableIfCtrlKeyDown && ModifierKeys::getCurrentModifiers().isCtrlDown());
}

int PMixDocument::snapPosition (int pos) const noexcept
{
  if (isSnapActive (true))
  {
    jassert (snapGridPixels > 0);
    pos = ((pos + snapGridPixels * 1024 + snapGridPixels / 2) / snapGridPixels - 1024) * snapGridPixels;
  }
  
  return pos;
}

void PMixDocument::setSnappingGrid (const int numPixels, const bool active, const bool shown)
{
  if (numPixels != snapGridPixels
      || active != snapActive
      || shown != snapShown)
  {
    snapGridPixels = numPixels;
    snapActive = active;
    snapShown = shown;
    changed();
  }
}

void PMixDocument::setComponentOverlayOpacity (const float alpha)
{
  if (alpha != componentOverlayOpacity)
  {
    componentOverlayOpacity = alpha;
    changed();
  }
}

void PMixDocument::beginTransaction()
{
  getUndoManager().beginNewTransaction();
}

void PMixDocument::beginTransaction (const String& name)
{
  getUndoManager().beginNewTransaction (name);
}

bool PMixDocument::perform (UndoableAction* const action, const String& actionName)
{
  return undoManager.perform (action, actionName);
}

void PMixDocument::initialize()
{
  InternalPluginFormat internalFormat;
  
  addNode(internalFormat.getDescriptionFor (InternalPluginFormat::audioInputNode),  0.5f,  0.1f);
  addNode(internalFormat.getDescriptionFor (InternalPluginFormat::midiInputNode),   0.25f, 0.1f);
  addNode(internalFormat.getDescriptionFor (InternalPluginFormat::audioOutputNode), 0.5f,  0.9f);
  
  setChangedFlag (false);
}

String PMixDocument::getLibraryPath()
{
  String fullLibraryPath;

#if JUCE_MAC
#if PMIX_PLUGIN
  CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR("com.OliLarkin.pMixPlugin"));
#else
  // OSX only : access to the pMix bundle
  CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR("com.OliLarkin.pMix"));
#endif
  CFURLRef ref = CFBundleCopyBundleURL(bundle);
  UInt8 bundlePath[512];
  Boolean res = CFURLGetFileSystemRepresentation(ref, true, bundlePath, 512);
  jassert(res);
  fullLibraryPath << (const char*) bundlePath << FAUST_LIBRARY_PATH;
#endif //JUCE_MAC
  
  return fullLibraryPath;
}

DynamicObject* PMixDocument::getPresetWithUID(NodeID nodeID, const int presetId) const
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));
  
  DynamicObject* returnObj = nullptr;
  
  if (node != nullptr)
  {
    Array<var>* presetsArr = node->properties.getVarPointer("presets")->getArray();
    
    for(int i = 0; i< presetsArr->size(); i++)
    {
      DynamicObject* thePreset = presetsArr->getReference(i).getDynamicObject();
      int thePresetUID = thePreset->getProperty("uid");
      
      if(thePresetUID == presetId)
      {
        returnObj = thePreset;
        break;
      }
    }
  }
  
  return returnObj;
}

void PMixDocument::addPreset(NodeID nodeID, double x, double y)
{
  AudioProcessorGraph::Node::Ptr node = getNodeForId(nodeID);
  AudioPluginInstance* plugin = dynamic_cast <AudioPluginInstance*> (node->getProcessor());

  var* presetsArr = node->properties.getVarPointer("presets");
  
  Array<var> paramValues;
  
  for(int p=0; p<plugin->getNumParameters(); p++)
  {
    paramValues.add(plugin->getParameter(p));
  }

  String name;
  name << "Preset " << presetsArr->getArray()->size() + 1;
  DynamicObject* obj = new DynamicObject();
  obj->setProperty("name", name);
  obj->setProperty("x", x);
  obj->setProperty("y", y);
  obj->setProperty("radius", 1.);
  obj->setProperty("hidden", false);
//  obj->setProperty("distance", 0.);
  obj->setProperty("coeff", 1.);
  obj->setProperty("state", paramValues);
  obj->setProperty("uid", getNextPresetUID());
  
  var preset = var(obj);
  presetsArr->append(preset);

  setNodeIPos(nodeID, x, y);
  
  changed();
}

void PMixDocument::removePreset(NodeID nodeID, const int presetId)
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));
  
  if (node != nullptr)
  {
    Array<var>* presetsArr = node->properties.getVarPointer("presets")->getArray();
    
    int indexToRemove = -1;
    
    for(int i = 0; i< presetsArr->size(); i++)
    {
      DynamicObject* obj = presetsArr->getReference(i).getDynamicObject();
      if((int) obj->getProperty("uid") == presetId)
      {
        indexToRemove = i;
        break;
      }
    }
    
    if(indexToRemove > -1) {
      presetsArr->remove(indexToRemove);
    }
  }
  
  changed();
}

void PMixDocument::setPresetPosition (NodeID nodeID, const int presetId, double x, double y)
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));
  
  if (node != nullptr)
  {
    DynamicObject* obj = getPresetWithUID(nodeID, presetId);
    
    jassert(obj != nullptr);
    
    obj->setProperty("x", jlimit (0.0, 1.0, x));
    obj->setProperty("y", jlimit (0.0, 1.0, y));
    
    updateCoefficients(node);
  }
}

void PMixDocument::getPresetPosition (NodeID nodeID, const int presetId, double& x, double& y) const
{
  x = y = 0;
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));
  
  if (node != nullptr)
  {
    DynamicObject* obj = getPresetWithUID(nodeID, presetId);
    
    jassert(obj != nullptr);
    
    x = (double) obj->getProperty("x");
    y = (double) obj->getProperty("y");
  }
}

double PMixDocument::getPresetWeight(NodeID nodeID, const int presetId)
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));
  
  if (node != nullptr)
  {
    DynamicObject* obj = getPresetWithUID(nodeID, presetId);
    
    jassert(obj != nullptr);
    return (double) obj->getProperty("coeff");
  }
  
  return 0.;
}

void PMixDocument::setPresetName(NodeID nodeID, const int presetId, String newName)
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));
  
  if (node != nullptr)
  {
    DynamicObject* obj = getPresetWithUID(nodeID, presetId);
    obj->setProperty("name", newName);
  }
  
  changed();
}

int PMixDocument::getNumPresetsForNode(NodeID nodeID)
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));
  
  if(node)
    return node->properties.getVarPointer("presets")->getArray()->size();
  else
    return 0;
}

void PMixDocument::setNodeColour(NodeID nodeID, const Colour colour)
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));

  if (node != nullptr)
  {
    node->properties.set("colour", colour.toString());
    changed();
  }
}

Colour PMixDocument::getNodeColour(NodeID nodeID) const
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));

  Colour clr = Colours::red;
  
  if (node != nullptr)
  {
    clr = Colour::fromString(node->properties ["colour"].toString());
  }
  
  return clr;
}

void PMixDocument::setParameterToInterpolate(NodeID nodeID, const int paramIdx, bool interpolate)
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));
  
  if (node != nullptr)
  {
    Array<var>* params = node->properties.getVarPointer("params")->getArray();
    
    if (interpolate)
    {
      params->addIfNotAlreadyThere(paramIdx);
    }
    else
    {
      params->removeAllInstancesOf(paramIdx);
    }
  }
}

bool PMixDocument::getParameterIsInterpolated(NodeID nodeID, const int paramIdx)
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));
  
  if (node != nullptr)
  {
    Array<var>* params = node->properties.getVarPointer("params")->getArray();
    
    return params->contains(paramIdx);
  }
  
  return false;
}

void PMixDocument::setNodeIPos(NodeID nodeID, double x, double y)
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));
  
  if (node != nullptr)
  {
    node->properties.set("iposx", x);
    node->properties.set("iposy", y);

    updateCoefficients(node);
  }
}

void PMixDocument::getNodeIPos(NodeID nodeID, double& x, double& y) const
{
  const AudioProcessorGraph::Node::Ptr node (audioEngine.getGraph().getNodeForId (nodeID));
  
  if (node != nullptr)
  {
    x = (double) node->properties["iposx"];
    y = (double) node->properties["iposy"];
  }
}

void PMixDocument::updateCoefficients(const AudioProcessorGraph::Node::Ptr node)
{  
  if (node != nullptr)
  {
    double iposx = (double) node->properties["iposx"];
    double iposy = (double) node->properties["iposy"];
    
    Array<var>* presetsArr = node->properties.getVarPointer("presets")->getArray();
    int numPresets = presetsArr->size();
    Array<double> distances;
    
    // work out the distances
    for (int presetIdx = 0; presetIdx < numPresets; presetIdx++)
    {
      DynamicObject* obj = presetsArr->getReference(presetIdx).getDynamicObject();
      double pposx = (double) obj->getProperty("x");
      double pposy = (double) obj->getProperty("y");
      
      double a = pposx - iposx;    //x dist
      double b = pposy - iposy;    //y dist
      
      if(a == 0. && b == 0.)
        distances.add(0.);
      else
        distances.add(sqrt(a*a+b*b));
    }
    
    // now do coeefficient
    
    double sumf = 0.; // sum of functions
    
    for (int presetIdx = 0; presetIdx < numPresets; presetIdx++)
    {
      DynamicObject* obj = presetsArr->getReference(presetIdx).getDynamicObject();
      bool hidden = (bool) obj->getProperty("hidden");
      double radius = (double) obj->getProperty("radius");
      
      double square = distances[presetIdx] * distances[presetIdx];
      
      if(!hidden)
      {
        sumf += (radius / square);
      }
    }
    
    for(int presetIdx = 0; presetIdx < numPresets; presetIdx++)
    {
      DynamicObject* obj = presetsArr->getReference(presetIdx).getDynamicObject();
      bool hidden = (bool) obj->getProperty("hidden");
      double radius = (double) obj->getProperty("radius");
      
      double coeff = 0.;
      
      if(distances[presetIdx] == 0.)
        coeff = 1.;
      else
      {
        double square = distances[presetIdx] * distances[presetIdx];
        
        if(!hidden)
        {
          coeff = (radius / square) / sumf;
        }
      }
      
      obj->setProperty("coeff", coeff);
    }
    
    node->properties.set("update", true);
  }
}

void PMixDocument::timerCallback()
{
  for (int i = getNumNodes(); --i >= 0;)
  {
    const AudioProcessorGraph::Node::Ptr node (audioEngine.getDoc().getNode (i));
    
    if (!InternalPluginFormat::isInternalFormat(node->getProcessor()->getName()))
    {
      bool needsUpdate = node->properties["update"];
      
      if (needsUpdate)
      {
        Array<var>* params = node->properties.getVarPointer("params")->getArray();
        Array<var>* presetsArr = node->properties.getVarPointer("presets")->getArray();

        //loop over all parameters marked for interpolating
        for (int item=0; item<params->size(); item++)
        {
          var parameterIdx = params->getReference(item);
          
          double sum = 0.;
          
          //loop over all presets
          for (int presetIdx=0; presetIdx < presetsArr->size(); presetIdx++)
          {
            DynamicObject* obj = presetsArr->getReference(presetIdx).getDynamicObject();
            double coeff = obj->getProperty("coeff");
            Array<var>* preset = obj->getProperty("state").getArray();
            sum = sum + (double(preset->getReference(parameterIdx)) * coeff);
          }
          
          node->getProcessor()->setParameterNotifyingHost(parameterIdx, (float) sum);
        }
        
        node->properties.set("update", false);
      }
    }
  }
}

