#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <set>

// ANA has stable APVTS targets (including explicit submodule and band IDs).
// As in AVA, audio-thread callbacks only try the lock and defer on contention.
class HostSlots
{
public:
    static constexpr int count=64;
    static juce::String id(int slot) { return "host_slot_"+juce::String::formatted("%02d",slot+1); }
    static juce::Identifier key(int slot) { return "ana.host_slot."+juce::String::formatted("%02d",slot+1)+".target"; }
    static juce::String letter(int slot) { return juce::String::charToString(juce::juce_wchar('A'+slot/26))+juce::String::charToString(juce::juce_wchar('A'+slot%26)); }
    HostSlots(juce::AudioProcessorValueTreeState& stateIn,std::function<void()> deferIn) : state(stateIn),defer(std::move(deferIn))
    {
        for(int i=0;i<count;++i) { slots[size_t(i)]=state.getParameter(id(i)); pending[size_t(i)].store(-1); gestures[size_t(i)].store(0); targetIndices[size_t(i)].store(-1); }
        refresh();
    }
    ~HostSlots() { const juce::ScopedLock guard(lock); for(int i=0;i<count;++i) endGestures(i); }
    const juce::CriticalSection& snapshotLock() const { return lock; }
    juce::String targetId(int slot) const { return juce::isPositiveAndBelow(slot,count)?state.state.getProperty(key(slot)).toString():juce::String(); }
    juce::String displayName(int slot) const { const auto target=targetId(slot); if(auto* p=state.getParameter(target)) return p->getName(256); return target; }
    bool assigned(const juce::String& target) const { for(int i=0;i<count;++i) if(targetId(i)==target) return true; return false; }
    bool toggle(const juce::String& target)
    {
        if(!validTarget(target)) return false;
        const juce::ScopedLock guard(lock); int free=-1;
        for(int i=0;i<count;++i) { const auto current=targetId(i); if(current==target) { clear(i); return true; } if(current.isEmpty() && free<0) free=i; }
        if(free<0) return false;
        state.state.setProperty(key(free),target,nullptr); refresh(); return true;
    }
    void clear(int slot)
    {
        if(!juce::isPositiveAndBelow(slot,count)) return;
        const juce::ScopedLock guard(lock); endGestures(slot); targets[size_t(slot)]=nullptr;
        pending[size_t(slot)].store(-1); gestures[size_t(slot)].store(0);
        state.state.removeProperty(key(slot),nullptr);
        const juce::ScopedValueSetter<bool> syncingGuard(syncing,true); slots[size_t(slot)]->setValueNotifyingHost(0);
    }
    void swap(int first,int second)
    {
        if(first==second || !juce::isPositiveAndBelow(first,count) || !juce::isPositiveAndBelow(second,count)) return;
        const juce::ScopedLock guard(lock); const auto a=targetId(first),b=targetId(second);
        const juce::ScopedValueSetter<bool> mutationGuard(mutating,true);
        for(int i:{first,second}) { endGestures(i); pending[size_t(i)].store(-1); gestures[size_t(i)].store(0); }
        const auto put=[&](int i,const juce::String& target) { if(target.isEmpty()) state.state.removeProperty(key(i),nullptr); else state.state.setProperty(key(i),target,nullptr); };
        put(first,b); put(second,a); refresh();
    }
    void propertyChanged(const juce::Identifier& property) { if(!mutating && property.toString().startsWith("ana.host_slot.")) refresh(); }
    void refresh()
    {
        const juce::ScopedLock guard(lock); const juce::ScopedValueSetter<bool> syncingGuard(syncing,true);
        const juce::ScopedValueSetter<bool> mutationGuard(mutating,true);
        std::set<juce::String> seen;
        for(int i=0;i<count;++i)
        {
            const auto index=size_t(i); const auto target=targetId(i);
            auto* p=validTarget(target) && seen.insert(target).second?state.getParameter(target):nullptr;
            if(p!=targets[index]) endGestures(i);
            if(!p && target.isNotEmpty()) state.state.removeProperty(key(i),nullptr);
            targets[index]=p; targetIndices[index].store(p?p->getParameterIndex():-1); pending[index].store(-1); gestures[index].store(0);
            slots[index]->setValueNotifyingHost(p?p->getValue():0.0f);
        }
    }
    void valueChanged(int parameterIndex,float value)
    {
        if(!std::isfinite(value)) return;
        const juce::ScopedTryLock guard(lock);
        if(guard.isLocked() && syncing) return;
        for(int i=0;i<count;++i) if(slots[size_t(i)]->getParameterIndex()==parameterIndex) { pending[size_t(i)].store(juce::jlimit(0.0f,1.0f,value),std::memory_order_release); break; }
        if(!guard.isLocked()) { defer(); return; } sync();
    }
    void gestureChanged(int parameterIndex,bool active)
    {
        const juce::ScopedTryLock guard(lock); if(guard.isLocked() && syncing) return;
        // Parameter indices are immutable; only assignment reads require the lock.
        for(int i=0;i<count;++i) if(slots[size_t(i)]->getParameterIndex()==parameterIndex) { gestures[size_t(i)].fetch_or(active?4u:8u); if(guard.isLocked()) sync(); else defer(); return; }
        for(int i=0;i<count;++i) if(targetIndices[size_t(i)].load()==parameterIndex) gestures[size_t(i)].fetch_or(active?1u:2u);
        if(!guard.isLocked()) { defer(); return; } sync();
    }
    void sync()
    {
        const juce::ScopedTryLock guard(lock); if(!guard.isLocked()) { defer(); return; } if(syncing) return;
        const juce::ScopedValueSetter<bool> syncingGuard(syncing,true);
        for(int i=0;i<count;++i)
        {
            const auto index=size_t(i); auto* target=targets[index]; const auto bits=gestures[index].exchange(0);
            if(target && (bits&1u) && !slotGesture[index]) { slotGesture[index]=true; slots[index]->beginChangeGesture(); }
            if(target && (bits&4u) && !sourceGesture[index]) { sourceGesture[index]=true; target->beginChangeGesture(); }
            const auto request=pending[index].exchange(-1);
            if(target && request>=0)
            {
                const auto plain=target->getNormalisableRange().snapToLegalValue(target->convertFrom0to1(request));
                const auto normalized=target->isBoolean()?(request>=0.5f?1.0f:0.0f):target->convertTo0to1(plain);
                if(std::abs(target->getValue()-normalized)>1.0e-7f) target->setValueNotifyingHost(normalized);
            }
            const auto value=target?target->getValue():0.0f;
            if(std::abs(slots[index]->getValue()-value)>1.0e-7f) slots[index]->setValueNotifyingHost(value);
            if((bits&2u) && slotGesture[index]) { slotGesture[index]=false; slots[index]->endChangeGesture(); }
            if((bits&8u) && sourceGesture[index]) { sourceGesture[index]=false; if(target) target->endChangeGesture(); }
        }
    }
private:
    juce::AudioProcessorValueTreeState& state; std::function<void()> defer;
    juce::CriticalSection lock; bool syncing=false,mutating=false;
    std::array<juce::RangedAudioParameter*,count> slots {},targets {};
    std::array<std::atomic<float>,count> pending;
    std::array<std::atomic<int>,count> targetIndices;
    std::array<std::atomic<unsigned>,count> gestures;
    std::array<bool,count> slotGesture {},sourceGesture {};
    bool validTarget(const juce::String& target) const { return target.isNotEmpty() && target!="animation-mode" && !target.startsWith("animation-topographic-") && !target.startsWith("host_slot_") && state.getParameter(target)!=nullptr; }
    void endGestures(int slot)
    {
        const auto index=size_t(slot); const juce::ScopedValueSetter<bool> syncingGuard(syncing,true);
        if(slotGesture[index]) { slotGesture[index]=false; slots[index]->endChangeGesture(); }
        if(sourceGesture[index]) { sourceGesture[index]=false; if(targets[index]) targets[index]->endChangeGesture(); }
    }
};
