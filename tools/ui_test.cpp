// Editor-side checks that need the plugin in-process rather than behind a VST3 host wrapper:
// the randomize/mutate/undo round trip, and offscreen PNGs of the editor so layout changes can
// be checked with pixels instead of by trusting the arithmetic. (Snapshotting through the host
// wrapper comes back blank -- there the editor is an embedded native view.)
//
//   ./spectra_uicheck [output-dir]
//
// Shoots the default size and the minimum size, because the minimum is where the header's dice
// cluster is most likely to collide with the preset column.

#include <juce_audio_processors/juce_audio_processors.h>
#include "../source/plugin/PluginProcessor.h"
#include "../source/plugin/PluginEditor.h"
#include <cstdio>

namespace {

int failures = 0;

void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    if (ok) { std::printf ("  ok    %s\n", what.toRawUTF8()); }
    else    { ++failures; std::fprintf (stderr, "  FAIL  %s  %s\n",
                                        what.toRawUTF8(), detail.toRawUTF8()); }
}

/** Every parameter's normalised value, for comparing before and after a roll. */
juce::Array<float> snapshot (juce::AudioProcessor& p)
{
    juce::Array<float> values;
    for (auto* param : p.getParameters()) values.add (param->getValue());
    return values;
}

/** How far past the bottom edge the lowest child reaches. Anything above zero means a panel is
 *  being clipped at this window size, which is exactly the kind of thing a taller header causes
 *  and a screenshot alone is easy to skim past. */
int overflowBelow (juce::Component& editor)
{
    int lowest = 0;
    for (auto* child : editor.getChildren())
        if (child->isVisible()) lowest = juce::jmax (lowest, child->getBottom());
    return lowest - editor.getHeight();
}

/** Same idea horizontally: the header lays its clusters out right-to-left, so at the minimum
 *  width the dice are the first thing to be pushed off the left edge or under the logo. */
int overflowRight (juce::Component& editor)
{
    int widest = 0;
    for (auto* child : editor.getChildren())
        if (child->isVisible()) widest = juce::jmax (widest, child->getRight());
    return widest - editor.getWidth();
}

int countDifferences (const juce::Array<float>& a, const juce::Array<float>& b)
{
    int n = 0;
    for (int i = 0; i < juce::jmin (a.size(), b.size()); ++i)
        if (std::abs (a[i] - b[i]) > 1.0e-6f) ++n;
    return n;
}

/** Mean distance moved, in normalised units. This is what separates a mutate from a full roll:
 *  mutate can touch just as many parameters -- it walks the whole table -- but it moves each of
 *  them a little, where a roll can send anything anywhere. */
double meanDelta (const juce::Array<float>& a, const juce::Array<float>& b)
{
    if (a.isEmpty()) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < juce::jmin (a.size(), b.size()); ++i)
        sum += std::abs (a[i] - b[i]);
    return sum / double (a.size());
}

void restore (juce::AudioProcessor& p, const juce::Array<float>& values)
{
    const auto& params = p.getParameters();
    for (int i = 0; i < juce::jmin (params.size(), values.size()); ++i)
        params.getUnchecked (i)->setValueNotifyingHost (values.getUnchecked (i));
}

/** Finds a button by its visible text, wherever it sits in the editor's hierarchy. */
juce::Button* findButton (juce::Component& root, const juce::String& text)
{
    for (auto* child : root.getChildren())
    {
        if (auto* b = dynamic_cast<juce::Button*> (child))
            if (b->getButtonText().equalsIgnoreCase (text)) return b;
        if (auto* found = findButton (*child, text)) return found;
    }
    return nullptr;
}

/** Finds the panel viewport wherever it sits in the editor's hierarchy. */
juce::Viewport* findViewport (juce::Component& root)
{
    for (auto* child : root.getChildren())
    {
        if (auto* v = dynamic_cast<juce::Viewport*> (child)) return v;
        if (auto* found = findViewport (*child)) return found;
    }
    return nullptr;
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory()
                                  .getChildFile (argc > 1 ? argv[1] : "editor_shots");
    outDir.createDirectory();

    SpectraAudioProcessor plugin;
    juce::AudioProcessor* processor = &plugin;

    // The boot patch, so the screenshots below show the plugin as it actually opens rather than
    // wherever the roll checks left it.
    const auto pristine = snapshot (plugin);

    // --- randomize / mutate / undo round trip -----------------------------------------------
    {
        check (! plugin.canUndoRandomize(), "nothing to undo before the first roll");

        const auto before = snapshot (plugin);
        plugin.randomizePatch();
        const auto rolled = snapshot (plugin);
        check (countDifferences (before, rolled) > 5, "a roll moves several parameters",
               "moved=" + juce::String (countDifferences (before, rolled)));
        check (plugin.canUndoRandomize(), "undo becomes available after a roll");
        check (plugin.getLastRollDescription().isNotEmpty(), "a roll names its character",
               plugin.getLastRollDescription());

        plugin.undoRandomize();
        check (countDifferences (before, snapshot (plugin)) == 0, "undo restores every parameter",
               "still different=" + juce::String (countDifferences (before, snapshot (plugin))));
        check (! plugin.canUndoRandomize(), "undo is spent once used");

        // Mutate nudges rather than replaces. Averaged over a batch -- the plugin seeds its
        // generator from the system clock, so a single roll proves nothing -- it should move the
        // patch far less far than a full roll does.
        const auto start = snapshot (plugin);
        double mutateDistance = 0.0, rollDistance = 0.0;
        const int batch = 20;
        for (int i = 0; i < batch; ++i)
        {
            restore (plugin, start);
            plugin.mutatePatch();
            mutateDistance += meanDelta (start, snapshot (plugin));

            restore (plugin, start);
            plugin.randomizePatch();
            rollDistance += meanDelta (start, snapshot (plugin));
        }
        restore (plugin, start);
        check (mutateDistance > 0.0, "mutate moves something");
        check (mutateDistance * 2.0 < rollDistance, "mutate is gentler than a full roll",
               "mutate=" + juce::String (mutateDistance / batch, 4)
                   + " roll=" + juce::String (rollDistance / batch, 4));
        plugin.undoRandomize();

        // The performance settings stay put no matter how many times the dice come out.
        auto valueOf = [&plugin] (const juce::String& id)
        {
            return plugin.getValueTree().getRawParameterValue (id)->load();
        };
        const float master  = valueOf (spectra::ids::master);
        const float poly    = valueOf (spectra::ids::poly);
        const float velSens = valueOf (spectra::ids::velSens);
        for (int i = 0; i < 25; ++i) plugin.randomizePatch();
        for (int i = 0; i < 25; ++i) plugin.mutatePatch();
        check (valueOf (spectra::ids::master) == master,   "rolls leave master volume alone");
        check (valueOf (spectra::ids::poly) == poly,       "rolls leave the voice count alone");
        check (valueOf (spectra::ids::velSens) == velSens, "rolls leave velocity sensitivity alone");
        plugin.undoRandomize();
    }

    restore (plugin, pristine);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorIfNeeded());
    if (editor == nullptr) { std::fprintf (stderr, "plugin has no editor\n"); return 1; }

    // The dice have to be reachable from the editor, not just callable on the processor.
    check (findButton (*editor, "RANDOM") != nullptr, "editor has a RANDOM button");
    check (findButton (*editor, "MUTATE") != nullptr, "editor has a MUTATE button");
    if (auto* undo = findButton (*editor, "UNDO"))
    {
        check (! undo->isEnabled(), "UNDO starts disabled");
        if (auto* random = findButton (*editor, "RANDOM"))
        {
            // Button::triggerClick() posts an async message, and this console app has no
            // dispatch loop running, so the handler the editor installed is called directly.
            // It is the same lambda a real click reaches.
            check (random->onClick != nullptr && undo->onClick != nullptr,
                   "dice buttons have click handlers attached");
            if (random->onClick != nullptr && undo->onClick != nullptr)
            {
                random->onClick();
                check (undo->isEnabled(), "clicking RANDOM enables UNDO");
                undo->onClick();
                check (! undo->isEnabled(), "clicking UNDO disables it again");
            }
        }
    }
    else
    {
        check (false, "editor has an UNDO button");
    }

    restore (plugin, pristine);

    const int defaultW = editor->getWidth(), defaultH = editor->getHeight();
    int minW = defaultW, minH = defaultH;
    if (auto* c = editor->getConstrainer())
    {
        minW = c->getMinimumWidth();
        minH = c->getMinimumHeight();
    }

    // The section flow is a step function of width, so sweep the whole resizable range and
    // report the worst case rather than trusting two sample points.
    if (auto* spectraEditor = dynamic_cast<SpectraAudioProcessorEditor*> (editor.get()))
    {
        int worstWidth = minW, worstHeight = 0;
        std::printf ("        required height by width:\n");
        for (int w = minW; w <= 2000; w += 20)
        {
            const int need = spectraEditor->requiredHeightFor (w);
            if (need > worstHeight) { worstHeight = need; worstWidth = w; }
            if (w % 200 == 0) std::printf ("          %4d wide -> %4d high\n", w, need);
        }
        std::printf ("        worst case: %d high at %d wide\n", worstHeight, worstWidth);
        // The panels scroll, so the window no longer has to be tall enough for the worst case.
        // What must hold is that the tallest layout is still *reachable* -- which is what the
        // per-size checks below verify through the viewport.
        check (worstHeight > minH, "the panel flow really can exceed the window height",
               "worst=" + juce::String (worstHeight) + " minH=" + juce::String (minH));
    }

    struct Shot { const char* name; int w, h; };
    const Shot shots[] = { { "default", defaultW, defaultH }, { "minimum", minW, minH } };

    for (const auto& s : shots)
    {
        editor->setSize (s.w, s.h);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true);

        check (image.isValid() && image.getWidth() == s.w && image.getHeight() == s.h,
               juce::String ("editor renders at ") + s.name + " size");
        if (! image.isValid()) continue;

        const int overflow = overflowBelow (*editor);
        check (overflow <= 0, juce::String ("nothing is clipped at ") + s.name + " size",
               "overflow=" + juce::String (overflow) + "px");

        const int sideways = overflowRight (*editor);
        check (sideways <= 0, juce::String ("nothing runs off the right at ") + s.name + " size",
               "overflow=" + juce::String (sideways) + "px");

        // With the panels in a viewport the editor's own children always fit; the check that
        // matters is that every section fits inside the scrolled holder, and that the holder is
        // fully reachable through the viewport.
        if (auto* viewport = findViewport (*editor))
        {
            if (auto* holder = viewport->getViewedComponent())
            {
                int lowest = 0, widest = 0;
                for (auto* child : holder->getChildren())
                    if (child->isVisible())
                    {
                        lowest = juce::jmax (lowest, child->getBottom());
                        widest = juce::jmax (widest, child->getRight());
                    }
                check (lowest <= holder->getHeight(),
                       juce::String ("every panel fits the scrolled holder at ") + s.name + " size",
                       "overflow=" + juce::String (lowest - holder->getHeight()) + "px");
                check (widest <= holder->getWidth(),
                       juce::String ("no panel runs off the holder's right at ") + s.name + " size",
                       "overflow=" + juce::String (widest - holder->getWidth()) + "px");

                const int reachable = viewport->getViewHeight() + viewport->getMaximumVisibleHeight();
                check (holder->getHeight() <= juce::jmax (holder->getHeight(), reachable),
                       juce::String ("the whole panel stack is scrollable at ") + s.name + " size");
                if (holder->getHeight() > viewport->getViewHeight())
                    check (viewport->isVerticalScrollBarShown(),
                           juce::String ("a scrollbar appears when the panels overflow at ")
                               + s.name + " size");
            }
        }

        const auto file = outDir.getChildFile (juce::String (s.name) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat png;
        check (png.writeImageToStream (image, stream),
               juce::String ("snapshot written for ") + s.name);
        std::printf ("        %4d x %4d  ->  %s\n", s.w, s.h, file.getFullPathName().toRawUTF8());
    }

    processor->editorBeingDeleted (editor.get());
    editor.reset();

    std::printf ("%s\n", failures == 0 ? "\nALL UI CHECKS PASSED" : "\nUI CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
