# HISE UI Style Guide

This document describes the visual and coding conventions for UI components in the HISE codebase.

**Related Documentation:**
- [../api/mcp-laf-integration.md](../api/mcp-laf-integration.md) - LAF integration for custom styling

> **IMPORTANT FOR AI ASSISTANTS**: NEVER attempt to build HISE. The user will always build it themselves.

---

## Fonts

Use the predefined font macros (approximately 14px size):

```cpp
g.setFont(GLOBAL_FONT());      // Regular text
g.setFont(GLOBAL_BOLD_FONT()); // Bold/emphasized text
```

## Colors

### Status/Semantic Colors

| Purpose | Macro | Hex Value | Description |
|---------|-------|-----------|-------------|
| Accent/Active | `Colour(SIGNAL_COLOUR)` | `0xFF90FFB1` | Neon green, primary accent |
| Success | `Colour(HISE_OK_COLOUR)` | `0xFF4E8E35` | Green for success states |
| Warning | `Colour(HISE_WARNING_COLOUR)` | `0xFFFFBA00` | Orange for warnings |
| Error | `Colour(HISE_ERROR_COLOUR)` | `0xFFBB3434` | Red for errors |

### General UI Colors

| Purpose | Value |
|---------|-------|
| Background | `Colour(0xFF333333)` or `#333333` |
| Text | `Colours::white.withAlpha(0.8f)` |
| Dimmed/Inactive | `Colours::white.withAlpha(0.5f)` |
| Borders/Lines | `Colours::black.withAlpha(0.5f)` |

### Backend Icon Colors

```cpp
#define BACKEND_ICON_COLOUR_ON  0xCCFFFFFF
#define BACKEND_ICON_COLOUR_OFF 0xFF333333
```

## Look and Feel

### GlobalHiseLookAndFeel

The default LAF for HISE components. Use as a member and assign to components:

```cpp
class MyComponent : public Component
{
    GlobalHiseLookAndFeel laf;
    
    MyComponent()
    {
        someButton.setLookAndFeel(&laf);
    }
};
```

### Subclassing

If `GlobalHiseLookAndFeel` doesn't provide overrides for your component type, subclass it:

```cpp
struct MyLookAndFeel : public GlobalHiseLookAndFeel
{
    void drawSomeWidget(Graphics& g, ...) override
    {
        // Custom drawing code
    }
};
```

### Useful LAF Methods

- `GlobalHiseLookAndFeel::drawFake3D(g, bounds)` - Draws a subtle 3D effect
- `GlobalHiseLookAndFeel::setDefaultColours(component)` - Applies default color scheme

## Icon Buttons

Use `HiseShapeButton` with a `PathFactory` for icon buttons:

```cpp
// Create path factory that returns your icons
struct MyPathFactory : public PathFactory
{
    Path createPath(const String& id) const override
    {
        Path p;
        LOAD_EPATH_IF_URL("save", SampleMapIcons::saveSampleMap);
        LOAD_EPATH_IF_URL("folder", EditorIcons::openFile);
        return p;
    }
};

// Create button
MyPathFactory factory;
auto button = new HiseShapeButton("save", listener, factory);
```

### Available Icons

Icons are defined in `hi_binary_data.h`:
- `SampleMapIcons::saveSampleMap` - Save/floppy disk icon
- `SampleMapIcons::loadSampleMap` - Load/folder icon
- `EditorIcons::saveFile` - Alternative save icon
- See `hi_tools/hi_binary_data/` for full list

### HiseShapeButton Colors

```cpp
button->onColour = Colour(SIGNAL_COLOUR);  // When toggled on
button->offColour = Colours::white;         // When toggled off
```

## Progress Bars

`AlertWindowLookAndFeel` (parent of `GlobalHiseLookAndFeel`) provides `drawProgressBar()`:

```cpp
void drawProgressBar(Graphics& g, ProgressBar& progressBar, 
                     int width, int height,
                     double progress, const String& textToShow)
```

Style: Dark background, bright outline, gradient fill for progress.

## Animations

Use `juce::Timer` for UI animations:

```cpp
class MyComponent : public Component, public Timer
{
    void timerCallback() override
    {
        // Update animation state
        repaint();
    }
    
    void startAnimation()
    {
        startTimerHz(30);  // 30 fps
    }
};
```

## File Dialogs

Use native file choosers for folder/file selection:

```cpp
FileChooser chooser("Select folder", 
                    File::getSpecialLocation(File::userDesktopDirectory));

if (chooser.browseForDirectory())
{
    File selectedFolder = chooser.getResult();
}
```

## Component Layout Guidelines

### Status Bars

- Height: ~28px (compact but readable)
- Background: `Colour(0xFF333333)`
- Top border: 1px `Colours::black.withAlpha(0.5f)`
- Padding: 4px internal margins

### General Spacing

- Standard padding: 4px
- Between elements: 8px
- Progress bar internal padding: 6px vertical, 4px horizontal

## Code Conventions

### Leak Detection

Always include leak detector in component classes:

```cpp
JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MyComponent)
```

### Member Variables

- No prefix convention (not `m_` or `_`)
- Use descriptive names
- Group by purpose with comments

### Comments

```cpp
/** Documentation comment for classes/methods */
// Inline comment for implementation details
```

## Color Application Examples

```cpp
// Success state indicator
g.setColour(Colour(HISE_OK_COLOUR));
g.fillEllipse(indicatorBounds);

// Active/running state with pulse
g.setColour(Colour(SIGNAL_COLOUR).withAlpha(pulseAlpha));
g.fillEllipse(indicatorBounds);

// Error state
g.setColour(Colour(HISE_ERROR_COLOUR));
g.drawText("Failed", bounds, Justification::centred);

// Standard text
g.setColour(Colours::white.withAlpha(0.8f));
g.setFont(GLOBAL_FONT());
g.drawText("Label", bounds, Justification::centredLeft);

// Emphasized text
g.setColour(Colours::white.withAlpha(0.8f));
g.setFont(GLOBAL_BOLD_FONT());
g.drawText("TITLE", bounds, Justification::centred);
```
