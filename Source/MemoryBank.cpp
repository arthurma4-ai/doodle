#include "MemoryBank.h"
#include <BinaryData.h>
namespace {
void writePattern(juce::XmlElement& xml,const finger::Pattern& p)
{
    xml.setAttribute("swing",p.swing);xml.setAttribute("barTiming",p.barTiming);
    for(size_t i=0;i<p.lanes.size();++i){auto* row=xml.createNewChildElement("Lane");row->setAttribute("sample",(int)i);row->setAttribute("bits",juce::String::toHexString((juce::int64)p.lanes[i]));row->setAttribute("steps",i<p.lengths.size()?p.lengths[i]:16);row->setAttribute("rotation",i<p.rotations.size()?p.rotations[i]:0);row->setAttribute("speed",i<p.speeds.size()?p.speeds[i]:1.0);row->setAttribute("enabled",i<p.enabled.size()?p.enabled[i]!=0:p.lanes[i]!=0);}
}
finger::Pattern readPattern(const juce::XmlElement& xml,size_t count)
{
    finger::Pattern p;p.lanes.assign(count,0);p.lengths.assign(count,16);p.speeds.assign(count,1);p.enabled.assign(count,0);p.rotations.assign(count,0);p.swing=juce::jlimit(50.,75.,xml.getDoubleAttribute("swing",50));p.barTiming=xml.getBoolAttribute("barTiming",true);
    for(auto* row:xml.getChildIterator()){const int i=row->getIntAttribute("sample",-1);if(i<0||(size_t)i>=count)continue;p.lanes[(size_t)i]=(uint32_t)row->getStringAttribute("bits").getHexValue64();p.lengths[(size_t)i]=juce::jlimit(1,32,row->getIntAttribute("steps",16));p.speeds[(size_t)i]=juce::jlimit(.125,8.,row->getDoubleAttribute("speed",1));p.rotations[(size_t)i]=row->getIntAttribute("rotation",0);p.enabled[(size_t)i]=row->getBoolAttribute("enabled",p.lanes[(size_t)i]!=0);}
    return p;
}
}
std::unique_ptr<juce::XmlElement> writeMemory(const MemoryBank& bank)
{
    auto xml=std::make_unique<juce::XmlElement>("Memory");xml->setAttribute("name",bank.name);xml->setAttribute("mode",(int)bank.sequences.mode);
    auto* samples=xml->createNewChildElement("Samples");for(const auto& s:bank.samples){auto* row=samples->createNewChildElement("Sample");row->setAttribute("path",s.path);row->setAttribute("start",s.start);row->setAttribute("end",s.end);row->setAttribute("tune",s.tune);row->setAttribute("ratio",s.ratio);row->setAttribute("stretch",s.stretch);row->setAttribute("note",s.note);}
    auto* keys=xml->createNewChildElement("Keys");for(int i=0;i<finger::patternCount;++i){auto* key=keys->createNewChildElement("Key");key->setAttribute("index",i);writePattern(*key,bank.sequences.patterns[(size_t)i]);}
    writePattern(*xml->createNewChildElement("Draft"),bank.draft);auto* vault=xml->createNewChildElement("Vault");for(const auto& v:bank.vault){auto* row=vault->createNewChildElement("Pattern");row->setAttribute("name",v.name);writePattern(*row,v.pattern);}return xml;
}
MemoryBank readMemory(const juce::XmlElement& xml)
{
    MemoryBank bank;bank.name=xml.getStringAttribute("name","Memory");bank.sequences.mode=(finger::PlayMode)juce::jlimit(0,2,xml.getIntAttribute("mode",0));
    if(auto* samples=xml.getChildByName("Samples"))for(auto* row:samples->getChildIterator()){PadSettings s;s.path=row->getStringAttribute("path");s.start=row->getDoubleAttribute("start",0);s.end=row->getDoubleAttribute("end",1);s.tune=row->getDoubleAttribute("tune",0);s.ratio=row->getDoubleAttribute("ratio",1);s.stretch=row->getBoolAttribute("stretch",false);s.note=row->getIntAttribute("note",36);bank.samples.push_back(s);}
    for(auto& pattern:bank.sequences.patterns){pattern.lanes.assign(bank.samples.size(),0);pattern.lengths.assign(bank.samples.size(),16);pattern.speeds.assign(bank.samples.size(),1);pattern.enabled.assign(bank.samples.size(),0);pattern.barTiming=true;}
    if(auto* keys=xml.getChildByName("Keys"))for(auto* key:keys->getChildIterator()){const int i=key->getIntAttribute("index",-1);if(i>=0&&i<finger::patternCount)bank.sequences.patterns[(size_t)i]=readPattern(*key,bank.samples.size());}
    if(auto* draft=xml.getChildByName("Draft"))bank.draft=readPattern(*draft,bank.samples.size());else bank.draft=bank.sequences.patterns[0];
    if(auto* vault=xml.getChildByName("Vault"))for(auto* row:vault->getChildIterator())bank.vault.push_back({row->getStringAttribute("name","Pattern"),readPattern(*row,bank.samples.size())});return bank;
}
juce::Result exportMemory(MemoryBank bank,const juce::File& folder)
{
    auto result=folder.getChildFile("Samples").createDirectory();if(result.failed())return result;
    for(size_t i=0;i<bank.samples.size();++i){auto& s=bank.samples[i];if(s.path.isEmpty())continue;juce::String name=s.path.startsWith("factory:")?s.path.fromFirstOccurrenceOf(":",false,false)+".wav":juce::File(s.path).getFileName();auto relative="Samples/"+juce::String((int)i)+"-"+name;auto destination=folder.getChildFile(relative);
        if(s.path.startsWith("factory:")){int bytes=0;auto key=s.path.fromFirstOccurrenceOf(":",false,false)+"_wav";auto* data=BinaryData::getNamedResource(key.toRawUTF8(),bytes);if(!data||!destination.replaceWithData(data,(size_t)bytes))return juce::Result::fail("Could not save "+name);}
        else if(juce::File(s.path)!=destination&&!juce::File(s.path).copyFileTo(destination))return juce::Result::fail("Missing or unreadable sample: "+s.path);s.path=relative;
    }
    if(!writeMemory(bank)->writeTo(folder.getChildFile("bank.xml")))return juce::Result::fail("Could not save bank.xml");return juce::Result::ok();
}
MemoryBank importMemory(const juce::File& manifest)
{
    auto xml=juce::XmlDocument::parse(manifest);if(!xml||!xml->hasTagName("Memory"))throw std::runtime_error("Choose a memory folder's bank.xml");auto bank=readMemory(*xml);for(auto& s:bank.samples)if(!s.path.isEmpty()&&!s.path.startsWith("factory:")&&!juce::File::isAbsolutePath(s.path))s.path=manifest.getParentDirectory().getChildFile(s.path).getFullPathName();return bank;
}
