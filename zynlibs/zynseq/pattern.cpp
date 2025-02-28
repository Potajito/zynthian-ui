#include "pattern.h"
#include <cmath>

/**    Pattern class methods implementation **/

Pattern::Pattern(uint32_t beats, uint32_t stepsPerBeat) :
    m_nBeats(beats),
    m_nStepsPerBeat(stepsPerBeat)
{
    setStepsPerBeat(stepsPerBeat);
    resetSnapshots();
}

Pattern::Pattern(Pattern* pattern) {
	*this = *pattern;
}

Pattern::~Pattern()
{
    clear();
	for (StepEventVector* sev : m_vSnapshots) {
		clearStepEventVector(sev);
        delete(sev);
    }
}

// copy assignment
Pattern& Pattern::operator=(Pattern& p)
{
    // Guard self assignment
    if (this == &p)
    	return *this;
	clear();
    m_nBeats = p.getBeatsInPattern();
    setStepsPerBeat(p.getStepsPerBeat());
    m_nScale = p.m_nScale;
    m_nTonic = p.m_nTonic;
    m_nRefNote = p.m_nRefNote;
	m_bQuantizeNotes = p.m_bQuantizeNotes;
    m_nSwingDiv = p.m_nSwingDiv;
    m_fSwingAmount = p.m_fSwingAmount;
    m_fHumanTime = p.m_fHumanTime;
    m_fHumanVelo = p.m_fHumanVelo;
    m_fPlayChance = p.m_fPlayChance;
    m_nZoom = p.m_nZoom;
	// Copy Events
    uint32_t i = 0;
    while (StepEvent* ev = p.getEventAt(i)) {
        addEvent(ev);
        i++;
    }
    resetSnapshots();
    return *this;
}

StepEvent* Pattern::addEvent(uint32_t position, uint8_t command, uint8_t value1, uint8_t value2, float duration, float offset)
{
    //Delete overlapping events
    uint8_t nStutterCount = 0;
    uint8_t nStutterDur = 1;
    uint8_t nFirstNote = 0;
    for(auto it = m_vEvents.begin(); it!=m_vEvents.end(); ++it)
    {
        uint32_t nEventStart = position;
        float fEventEnd = nEventStart + duration;
        uint32_t nCheckStart = (*it)->getPosition();
        float fCheckEnd = nCheckStart + (*it)->getDuration();
        bool bOverlap = (nCheckStart >= nEventStart && nCheckStart < fEventEnd) || (fCheckEnd > nEventStart && fCheckEnd <= fEventEnd);
        if(bOverlap && (*it)->getCommand() == command && (*it)->getValue1start() == value1)
        {
            if(!nFirstNote)
            {
                nStutterCount = (*it)->getStutterCount();
                nStutterDur = (*it)->getStutterDur();
                nFirstNote = 1;
            }
            delete *it;
            it = m_vEvents.erase(it) - 1;
            if(it == m_vEvents.end())
                break;
        }
    }
    uint32_t nTime = position % (m_nBeats * m_nStepsPerBeat);
    auto it = m_vEvents.begin();
    for(; it != m_vEvents.end(); ++it)
    {
        if((*it)->getPosition() > position)
            break;
    }
    auto itInserted = m_vEvents.insert(it, new StepEvent(position, command, value1, value2, duration, offset));
    (*itInserted)->setStutterCount(nStutterCount);
    (*itInserted)->setStutterDur(nStutterDur);
    return *itInserted;
}

StepEvent* Pattern::addEvent(StepEvent* pEvent)
{
    StepEvent* sev = addEvent(pEvent->getPosition(), pEvent->getCommand(), pEvent->getValue1start(), pEvent->getValue2start(), pEvent->getDuration());
    sev->setValue1end(pEvent->getValue1end());
    sev->setValue2end(pEvent->getValue2end());
    sev->setStutterCount(pEvent->getStutterCount());
    sev->setStutterDur(pEvent->getStutterDur());
    return sev;
}

void Pattern::deleteEvent(uint32_t position, uint8_t command, uint8_t value1)
{
    for(auto it = m_vEvents.begin(); it != m_vEvents.end(); ++it)
    {
        if((*it)->getPosition() == position && (*it)->getCommand() == command && (*it)->getValue1start() == value1)
        {
            delete *it;
            m_vEvents.erase(it);
            return;
        }
    }
}

bool Pattern::addNote(uint32_t step, uint8_t note, uint8_t velocity, float duration, float offset)
{
    //!@todo Should we limit note length to size of pattern?
    if(step >= (m_nBeats * m_nStepsPerBeat) || note > 127 || velocity > 127) // || duration > (m_nBeats * m_nStepsPerBeat))
        return false;
    addEvent(step, MIDI_NOTE_ON, note, velocity, duration, offset);
    return true;
}

void Pattern::removeNote(uint32_t step, uint8_t note)
{
    deleteEvent(step, MIDI_NOTE_ON, note);
}

int32_t Pattern::getNoteStart(uint32_t step, uint8_t note)
{
    for(StepEvent* ev : m_vEvents)
        if(ev->getPosition() <= step && int(std::ceil(ev->getPosition() + ev->getDuration())) > step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note)
            return ev->getPosition();
    return -1;
}

uint8_t Pattern::getNoteVelocity(uint32_t step, uint8_t note)
{
    for(StepEvent* ev : m_vEvents)
        if(ev->getPosition() == step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note)
        	return ev->getValue2start();
    return 0;
}

void Pattern::setNoteVelocity(uint32_t step, uint8_t note, uint8_t velocity)
{
    if(velocity > 127)
        return;
    for(StepEvent* ev : m_vEvents)
        if(ev->getPosition() == step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note)
        {
            ev->setValue2start(velocity);
            return;
        }
}

float Pattern::getNoteDuration(uint32_t step, uint8_t note)
{
    if(step >= (m_nBeats * m_nStepsPerBeat))
        return 0;
    for(StepEvent* ev : m_vEvents)
    {
        if(ev->getPosition() != step || ev->getCommand() != MIDI_NOTE_ON || ev->getValue1start() != note)
            continue;
        return ev->getDuration();
    }
    return 0.0;
}

float Pattern::getNoteOffset(uint32_t step, uint8_t note) {
    for(StepEvent* ev : m_vEvents)
        if(ev->getPosition() == step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note)
        	return ev->getOffset();
    return 0;
}

void Pattern::setNoteOffset(uint32_t step, uint8_t note, float offset) {
	if (offset < 0.0) offset = 0.0;
    else if (offset > 0.99) offset = 0.99;
    for (StepEvent* ev : m_vEvents) {
        if(ev->getPosition() == step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note) {
            ev->setOffset(offset);
            return;
        }
    }
}

void Pattern::setStutter(uint32_t step, uint8_t note, uint8_t count, uint8_t dur)
{
    for(StepEvent* ev : m_vEvents)
        if(ev->getPosition() == step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note)
        {
            if (ev->getDuration() > count * dur)
            {
                ev->setStutterCount(count);
                ev->setStutterDur(dur);
            }
            return;
        }
}

uint8_t Pattern::getStutterCount(uint32_t step, uint8_t note)
{
    for(StepEvent* ev : m_vEvents)
    {
        if(ev->getPosition() == step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note)
        {
            return ev->getStutterCount();
        }
    }
    return 0;
}

void Pattern::setStutterCount(uint32_t step, uint8_t note, uint8_t count)
{
    if(count > MAX_STUTTER_COUNT)
        return;
    for(StepEvent* ev : m_vEvents)
    {
        if(ev->getPosition() == step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note)
        {
           // if (ev->getDuration() > count * ev->getStutterDur())
                ev->setStutterCount(count);
            return;
        }
    }
}

uint8_t Pattern::getStutterDur(uint32_t step, uint8_t note)
{
    for(StepEvent* ev : m_vEvents)
        if(ev->getPosition() == step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note)
            return ev->getStutterDur();
    return 1;
}

void Pattern::setStutterDur(uint32_t step, uint8_t note, uint8_t dur)
{
    if(dur > MAX_STUTTER_DUR)
        return;
    for(StepEvent* ev : m_vEvents)
        if(ev->getPosition() == step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note)
        {
            //if (ev.getDuration() > dur * ev.getStutterCount())
                ev->setStutterDur(dur);
            return;
        }
}

uint8_t Pattern::getPlayChance(uint32_t step, uint8_t note) {
    for(StepEvent* ev : m_vEvents)
        if(ev->getPosition() == step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note)
            return ev->getPlayChance();
    return 100;
}

void Pattern::setPlayChance(uint32_t step, uint8_t note, uint8_t chance) {
    if (chance > 100) chance = 100;
    for (StepEvent* ev : m_vEvents)
        if (ev->getPosition() == step && ev->getCommand() == MIDI_NOTE_ON && ev->getValue1start() == note) {
            ev->setPlayChance(chance);
            return;
        }
}

bool Pattern::addProgramChange(uint32_t step, uint8_t program)
{
    if(step >= (m_nBeats * m_nStepsPerBeat) || program > 127)
        return false;
    removeProgramChange(step); // Only one PC per step
    addEvent(step, MIDI_PROGRAM, program);
    return true;
}

bool Pattern::removeProgramChange(uint32_t step)
{
    if(step >= (m_nBeats * m_nStepsPerBeat))
        return false;
    uint8_t program = getProgramChange(step);
    if(program == 0xFF)
        return false;
    deleteEvent(step, MIDI_PROGRAM, program);
    return true;
}

uint8_t Pattern::getProgramChange(uint32_t step)
{
    if(step >= (m_nBeats * m_nStepsPerBeat))
        return 0xFF;
    for(StepEvent* ev : m_vEvents)
    {
        if(ev->getPosition() != step || ev->getCommand() != MIDI_PROGRAM)
            continue;
        return ev->getValue1start();
    }
    return 0xFF;
}

void Pattern::addControl(uint32_t step, uint8_t control, uint8_t valueStart, uint8_t valueEnd, float duration)
{
    float fDuration = duration;
    if(step > (m_nBeats * m_nStepsPerBeat) || control > 127 || valueStart > 127|| valueEnd > 127 || fDuration > (m_nBeats * m_nStepsPerBeat))
        return;
    StepEvent* pControl = new StepEvent(step, control, valueStart, fDuration);
    pControl->setValue2end(valueEnd);
    StepEvent* pEvent = addEvent(step, MIDI_CONTROL, control, valueStart, fDuration);
    pEvent->setValue2end(valueEnd);
}

void Pattern::removeControl(uint32_t step, uint8_t control)
{
    deleteEvent(step, MIDI_CONTROL, control);
}

float Pattern::getControlDuration(uint32_t step, uint8_t control)
{
    //!@todo Implement getControlDuration
    return 0.0;
}

uint32_t Pattern::getSteps()
{
    return (m_nBeats * m_nStepsPerBeat);
}


uint32_t Pattern::getLength()
{
    return m_nBeats * PPQN;
}

uint32_t Pattern::getClocksPerStep()
{
    if(m_nStepsPerBeat > PPQN || m_nStepsPerBeat == 0)
        return 1;
    return PPQN / m_nStepsPerBeat;
}

bool Pattern::setStepsPerBeat(uint32_t value)
{
    float fScale = 1.0;
    if(m_nStepsPerBeat == 0 || m_nStepsPerBeat > PPQN)
        m_nStepsPerBeat = 4;
    else
        float fScale = float(value) / m_nStepsPerBeat;

    switch(value)
    {
        case 1:
        case 2:
        case 3:
        case 4:
        case 6:
        case 8:
        case 12:
        case 24:
            m_nStepsPerBeat = value;
            break;
        default:
            return false;
    }
    // Move events
    for(StepEvent* ev : m_vEvents)
    {
        ev->setPosition(ev->getPosition() * fScale);
        ev->setDuration(ev->getDuration() * fScale);
    }
    return true;
}

uint32_t Pattern::getStepsPerBeat()
{
    return m_nStepsPerBeat;
}

void Pattern::setBeatsInPattern(uint32_t beats)
{
    if (beats > 0)
        m_nBeats = beats;

    // Remove steps if shrinking
    size_t nIndex = 0;
    for(; nIndex < m_vEvents.size(); ++nIndex)
        if(m_vEvents[nIndex]->getPosition() >= (m_nBeats * m_nStepsPerBeat))
            break;
    m_vEvents.resize(nIndex);
}

uint32_t Pattern::getBeatsInPattern()
{
    return m_nBeats;
}

void Pattern::setScale(uint8_t scale)
{
    m_nScale = scale;
}

uint8_t Pattern::getScale()
{
    return m_nScale;
}

void Pattern::setTonic(uint8_t tonic)
{
    m_nTonic = tonic;
}

uint8_t Pattern::getTonic()
{
    return m_nTonic;
}

void Pattern::setSwingDiv(uint32_t div) {
	m_nSwingDiv = div;
}

uint32_t Pattern::getSwingDiv() {
	return m_nSwingDiv;
}

void Pattern::setSwingAmount(float amount) {
	m_fSwingAmount = amount;
}

float Pattern::getSwingAmount() {
	return m_fSwingAmount;
}

void Pattern::setHumanTime(float amount) {
	m_fHumanTime = amount;
}

float Pattern::getHumanTime() {
	return m_fHumanTime;
}

void Pattern::setHumanVelo(float amount) {
	m_fHumanVelo = amount;
}

float Pattern::getHumanVelo() {
	return m_fHumanVelo;
}

void Pattern::setPlayChance(float chance) {
	m_fPlayChance = chance;
}

float Pattern::getPlayChance() {
	return m_fPlayChance;
}

void Pattern::transpose(int value)
{
    // Check if any notes will be transposed out of MIDI note range (0..127)
    for(StepEvent* ev : m_vEvents)
    {
        if(ev->getCommand() != MIDI_NOTE_ON)
            continue;
        int note = ev->getValue1start() + value;
        if(note > 127 || note < 0)
            return;
    }

    for(auto it = m_vEvents.begin(); it != m_vEvents.end(); ++it)
    {
        if((*it)->getCommand() != MIDI_NOTE_ON)
            continue;
        int note = (*it)->getValue1start() + value;
        if(note > 127 || note < 0)
        {
            // Delete notes that have been pushed out of range
            //!@todo Should we squash notes that are out of range back in at ends? I don't think so.
            delete (*it);
            m_vEvents.erase(it);
        }
        else
        {
            (*it)->setValue1start(note);
            (*it)->setValue1end(note);
        }
    }
}

void Pattern::changeVelocityAll(int value)
{
    for(StepEvent* ev : m_vEvents)
    {
        if(ev->getCommand() != MIDI_NOTE_ON)
            continue;
        int vel = ev->getValue2start() + value;
        if(vel > 127)
            vel = 127;
        if(vel < 1)
            vel = 1;
        ev->setValue2start(vel);
    }
}

void Pattern::changeDurationAll(float value)
{
    for(StepEvent* ev : m_vEvents)
    {
        if(ev->getCommand() != MIDI_NOTE_ON)
            continue;
        float duration = ev->getDuration() + value;
        if(duration <= 0)
            return; // Don't allow jump larger than current value
        if(duration < 0.1) //!@todo How short should we allow duration change?
            duration = 0.1;
        ev->setDuration(duration);
    }
}

void Pattern::changeStutterCountAll(int value)
{
    for(StepEvent* ev : m_vEvents)
    {
        if(ev->getCommand() != MIDI_NOTE_ON)
            continue;
        int count = ev->getStutterCount() + value;
        if(count < 0)
            count = 0;
        if(count > 255)
            count = 255;
        ev->setStutterCount(count);
    }
}

void Pattern::changeStutterDurAll(int value)
{
    for(StepEvent* ev : m_vEvents)
    {
        if(ev->getCommand() != MIDI_NOTE_ON)
            continue;
        int dur = ev->getStutterDur() + value;
        if(dur < 1)
            dur = 1;
        if(dur > 255)
            dur = 255;
        ev->setStutterDur(dur);
    }
}

void Pattern::clear()
{
	clearStepEventVector(&m_vEvents);
}

StepEvent* Pattern::getEventAt(uint32_t index)
{
    if(index < 0 || index >= m_vEvents.size())
        return NULL;
    return m_vEvents[index];
}

int Pattern::getFirstEventAtStep(uint32_t step)
{
    int index;
    for(index = 0; index < m_vEvents.size(); ++index)
    {
        if(m_vEvents[index]->getPosition() == step)
            return index;
    }
    return -1;
}

size_t Pattern::getEvents()
{
    return m_vEvents.size();
}

uint8_t Pattern::getRefNote()
{
    return m_nRefNote;
}

void Pattern::setRefNote(uint8_t note)
{
    if(note < 128)
        m_nRefNote = note;
}

bool Pattern::getQuantizeNotes() {
	return m_bQuantizeNotes;
}

void Pattern::setQuantizeNotes(bool flag) {
	m_bQuantizeNotes = flag;
}

uint32_t Pattern::getLastStep()
{
    if(m_vEvents.size() == 0)
        return -1;
    uint32_t nStep = 0;
    for(StepEvent* ev : m_vEvents)
    {
        if(ev->getPosition() > nStep)
            nStep = ev->getPosition();
    }
    return nStep;
}

// Pattern Snapshots => Undo/Redo

void Pattern::clearStepEventVector(StepEventVector* sev) {
	if (sev && sev->size() > 0) {
    	for (StepEvent* ev : *sev)
        	delete ev;
    	sev->clear();
   	}
}

bool Pattern::restoreSnapshot(StepEventVector* sev) {
	if (sev) {
		clear();
		for(StepEvent* ev : *sev) {
			m_vEvents.push_back(new StepEvent(ev));
		}
		return true;
	}
	return false;
}

void Pattern::resetSnapshots() {
	// Destroy events
	for (StepEventVector* sev : m_vSnapshots) {
		clearStepEventVector(sev);
        delete(sev);
    }
    m_vSnapshots.clear();
    //m_vSnapshotPos = m_vSnapshots.end();
    saveSnapshot();
}

void Pattern::saveSnapshot() {
	// Delete snapshots from the current position, truncating the history
    if (m_vSnapshotPos < m_vSnapshots.end()) {
    	for (auto it = m_vSnapshotPos + 1; it < m_vSnapshots.end(); it++) {
    		clearStepEventVector(*it);
	    	delete(*it);
        }
        m_vSnapshots.erase(m_vSnapshotPos + 1, m_vSnapshots.end());
    }
    // Push snapshot at the end of the truncated history
    StepEventVector* ss = new StepEventVector();
    for (StepEvent* ev : m_vEvents) {
    	ss->push_back(new StepEvent(ev));
    }
    m_vSnapshots.push_back(ss);
    m_vSnapshotPos = m_vSnapshots.end() - 1;
}

bool Pattern::undo() {
	if (m_vSnapshotPos > m_vSnapshots.begin()) {
		// Undo one position
		m_vSnapshotPos--;
		return restoreSnapshot(*m_vSnapshotPos);
    }
    return false;
}

bool Pattern::redo() {
    if (m_vSnapshots.size() > 1 && m_vSnapshotPos < m_vSnapshots.end() - 1) {
    	// Redo one position
    	m_vSnapshotPos++;
		return restoreSnapshot(*m_vSnapshotPos);
    }
    return false;
}

bool Pattern::undoAll() {
	if (m_vSnapshotPos > m_vSnapshots.begin()) {
		// Undo one position
		m_vSnapshotPos = m_vSnapshots.begin();
		return restoreSnapshot(*m_vSnapshotPos);
    }
    return false;
}

bool Pattern::redoAll() {
    if (m_vSnapshots.size() > 1 && m_vSnapshotPos < m_vSnapshots.end() - 1) {
    	// Undo one position
    	m_vSnapshotPos = m_vSnapshots.end() - 1;
    	return restoreSnapshot(*m_vSnapshotPos);
    }
    return false;
}

