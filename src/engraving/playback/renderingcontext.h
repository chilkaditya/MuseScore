/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef MU_ENGRAVING_RENDERINGCONTEXT_H
#define MU_ENGRAVING_RENDERINGCONTEXT_H

#include "mpe/events.h"

#include "dom/chord.h"
#include "dom/note.h"
#include "dom/sig.h"
#include "dom/tie.h"

#include "playback/utils/arrangementutils.h"
#include "playback/utils/pitchutils.h"

namespace mu::engraving {
struct RenderingContext {
    muse::mpe::timestamp_t nominalTimestamp = 0;
    muse::mpe::duration_t nominalDuration = 0;
    muse::mpe::dynamic_level_t nominalDynamicLevel = 0;
    int nominalPositionStartTick = 0;
    int nominalPositionEndTick = 0;
    int nominalDurationTicks = 0;
    int positionTickOffset = 0;

    BeatsPerSecond beatsPerSecond = 0;
    TimeSigFrac timeSignatureFraction;

    muse::mpe::ArticulationType persistentArticulation = muse::mpe::ArticulationType::Undefined;
    muse::mpe::ArticulationMap commonArticulations;
    muse::mpe::ArticulationsProfilePtr profile;

    RenderingContext() = default;

    explicit RenderingContext(const muse::mpe::timestamp_t timestamp,
                              const muse::mpe::duration_t duration,
                              const muse::mpe::dynamic_level_t dynamicLevel,
                              const int posTick,
                              const int posTickOffset,
                              const int durationTicks,
                              const BeatsPerSecond& bps,
                              const TimeSigFrac& timeSig,
                              const muse::mpe::ArticulationType persistentArticulationType,
                              const muse::mpe::ArticulationMap& articulations,
                              const muse::mpe::ArticulationsProfilePtr profilePtr)
        : nominalTimestamp(timestamp),
        nominalDuration(duration),
        nominalDynamicLevel(dynamicLevel),
        nominalPositionStartTick(posTick),
        nominalPositionEndTick(posTick + durationTicks),
        nominalDurationTicks(durationTicks),
        positionTickOffset(posTickOffset),
        beatsPerSecond(bps),
        timeSignatureFraction(timeSig),
        persistentArticulation(persistentArticulationType),
        commonArticulations(articulations),
        profile(profilePtr)
    {}

    bool isValid() const
    {
        return profile
               && beatsPerSecond > 0
               && nominalDuration > 0
               && nominalDurationTicks > 0;
    }
};

inline muse::mpe::duration_t noteNominalDuration(const Note* note, const RenderingContext& ctx)
{
    if (!note->score()) {
        return durationFromTempoAndTicks(ctx.beatsPerSecond.val, note->chord()->actualTicks().ticks());
    }

    return durationFromStartAndTicks(note->score(), note->tick().ticks(), note->chord()->actualTicks().ticks(), 0);
}

struct NominalNoteCtx {
    voice_idx_t voiceIdx = 0;
    staff_idx_t staffIdx = 0;
    muse::mpe::timestamp_t timestamp = 0;
    muse::mpe::duration_t duration = 0;
    BeatsPerSecond tempo = 0;
    float userVelocityFraction = 0.f;

    muse::mpe::pitch_level_t pitchLevel = 0;

    RenderingContext chordCtx;

    explicit NominalNoteCtx(const Note* note, const RenderingContext& ctx)
        : voiceIdx(note->voice()),
        staffIdx(note->staffIdx()),
        timestamp(ctx.nominalTimestamp),
        duration(ctx.nominalDuration),
        tempo(ctx.beatsPerSecond),
        userVelocityFraction(note->userVelocityFraction()),
        pitchLevel(notePitchLevel(note->playingTpc(),
                                  note->playingOctave(),
                                  note->playingTuning())),
        chordCtx(ctx)
    {
        if (muse::RealIsEqual(userVelocityFraction, 0.f)) {
            return;
        }

        muse::mpe::dynamic_level_t userDynamicLevel = userVelocityFraction * muse::mpe::MAX_DYNAMIC_LEVEL;
        chordCtx.nominalDynamicLevel = std::clamp(userDynamicLevel,
                                                  muse::mpe::MIN_DYNAMIC_LEVEL,
                                                  muse::mpe::MAX_DYNAMIC_LEVEL);
    }
};

inline bool isNotePlayable(const Note* note, const muse::mpe::ArticulationMap& articualtionMap)
{
    if (!note->play()) {
        return false;
    }

    const Tie* tie = note->tieBack();

    if (tie) {
        if (!tie->startNote() || !tie->endNote()) {
            return false;
        }

        //!Note Checking whether the last tied note has any multi-note articulation attached
        //!     If so, we can't ignore such note
        if (!note->tieFor()) {
            for (const auto& pair : articualtionMap) {
                if (muse::mpe::isMultiNoteArticulation(pair.first) && !muse::mpe::isRangedArticulation(pair.first)) {
                    return true;
                }
            }
        }

        const Chord* firstChord = tie->startNote()->chord();
        const Chord* lastChord = tie->endNote()->chord();

        return !firstChord->containsEqualTremolo(lastChord);
    }

    return true;
}

inline muse::mpe::NoteEvent buildNoteEvent(NominalNoteCtx&& ctx)
{
    return muse::mpe::NoteEvent(ctx.timestamp,
                                ctx.duration,
                                static_cast<muse::mpe::voice_layer_idx_t>(ctx.voiceIdx),
                                static_cast<muse::mpe::staff_layer_idx_t>(ctx.staffIdx),
                                ctx.pitchLevel,
                                ctx.chordCtx.nominalDynamicLevel,
                                ctx.chordCtx.commonArticulations,
                                ctx.tempo.val,
                                ctx.userVelocityFraction);
}

inline muse::mpe::NoteEvent buildNoteEvent(NominalNoteCtx&& ctx, const muse::mpe::PitchCurve& pitchCurve)
{
    return muse::mpe::NoteEvent(ctx.timestamp,
                                ctx.duration,
                                static_cast<muse::mpe::voice_layer_idx_t>(ctx.voiceIdx),
                                static_cast<muse::mpe::staff_layer_idx_t>(ctx.staffIdx),
                                ctx.pitchLevel,
                                ctx.chordCtx.nominalDynamicLevel,
                                ctx.chordCtx.commonArticulations,
                                ctx.tempo.val,
                                ctx.userVelocityFraction,
                                pitchCurve);
}

inline muse::mpe::NoteEvent buildNoteEvent(const Note* note, const RenderingContext& ctx)
{
    return muse::mpe::NoteEvent(ctx.nominalTimestamp,
                                noteNominalDuration(note, ctx),
                                static_cast<muse::mpe::voice_layer_idx_t>(note->voice()),
                                static_cast<muse::mpe::staff_layer_idx_t>(note->staffIdx()),
                                notePitchLevel(note->playingTpc(), note->playingOctave(), note->playingTuning()),
                                ctx.nominalDynamicLevel,
                                ctx.commonArticulations,
                                ctx.beatsPerSecond.val,
                                note->userVelocityFraction());
}

inline muse::mpe::NoteEvent buildNoteEvent(NominalNoteCtx&& ctx, const muse::mpe::duration_t eventDuration,
                                           const muse::mpe::timestamp_t timestampOffset,
                                           const muse::mpe::pitch_level_t pitchLevelOffset)
{
    return muse::mpe::NoteEvent(ctx.timestamp + timestampOffset,
                                eventDuration,
                                static_cast<muse::mpe::voice_layer_idx_t>(ctx.voiceIdx),
                                static_cast<muse::mpe::staff_layer_idx_t>(ctx.staffIdx),
                                ctx.pitchLevel + pitchLevelOffset,
                                ctx.chordCtx.nominalDynamicLevel,
                                ctx.chordCtx.commonArticulations,
                                ctx.tempo.val,
                                ctx.userVelocityFraction);
}
}

#endif // MU_ENGRAVING_RENDERINGCONTEXT_H
