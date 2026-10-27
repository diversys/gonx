/*
 * Copyright 2026, Gonx contributors.
 * Distributed under the terms of the MIT License.
 *
 * One window with every kind of control, for checking a control look.
 */


#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <ScrollView.h>
#include <Slider.h>
#include <StatusBar.h>
#include <StringView.h>
#include <StringItem.h>
#include <TabView.h>
#include <TextControl.h>
#include <Window.h>


static BView*
controls_page()
{
	BPopUpMenu* menu = new BPopUpMenu("files");
	menu->AddItem(new BMenuItem("All files and folders", NULL));
	menu->AddItem(new BMenuItem("Folders", NULL));
	menu->ItemAt(0)->SetMarked(true);
	BMenuField* field = new BMenuField("field", NULL, menu);

	BPopUpMenu* disks = new BPopUpMenu("disks");
	disks->AddItem(new BMenuItem("All disks", NULL));
	disks->ItemAt(0)->SetMarked(true);
	BMenuField* onField = new BMenuField("on", "On", disks);

	BTextControl* query = new BTextControl("query", NULL, "", NULL);
	BTextControl* name = new BTextControl("name", "Query name:", "", NULL);
	BTextControl* disabledText = new BTextControl("disabled", "Disabled:",
		"can't edit", NULL);
	disabledText->SetEnabled(false);

	BCheckBox* trash = new BCheckBox("trash", "Include trash", NULL);
	BCheckBox* temporary = new BCheckBox("temp", "Temporary", NULL);
	temporary->SetValue(B_CONTROL_ON);
	BCheckBox* partial = new BCheckBox("partial", "Partly", NULL);
	partial->SetValue(B_CONTROL_PARTIALLY_ON);
	BCheckBox* off = new BCheckBox("off", "Disabled", NULL);
	off->SetValue(B_CONTROL_ON);
	off->SetEnabled(false);

	BRadioButton* one = new BRadioButton("one", "Identity", NULL);
	one->SetValue(B_CONTROL_ON);
	BRadioButton* two = new BRadioButton("two", "Power", NULL);

	BButton* search = new BButton("search", "Search", NULL);
	BButton* cancel = new BButton("cancel", "Cancel", NULL);
	BButton* disabled = new BButton("dis", "Disabled", NULL);
	disabled->SetEnabled(false);
	BButton* popUp = new BButton("pop", "Pop-up", NULL);
	popUp->SetBehavior(BButton::B_POP_UP_BEHAVIOR);

	BBox* group = new BBox("group");
	BView* inside = BLayoutBuilder::Group<>(B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(query)
		.View();
	group->AddChild(inside);

	BView* page = BLayoutBuilder::Group<>(B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL)
			.Add(field)
			.AddGlue()
			.Add(onField)
		.End()
		.Add(group)
		.AddGrid()
			.AddTextControl(name, 0, 0)
			.AddTextControl(disabledText, 0, 1)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(trash)
			.Add(temporary)
			.Add(partial)
			.Add(off)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(one)
			.Add(two)
			.AddGlue()
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(popUp)
			.Add(disabled)
			.AddGlue()
			.Add(cancel)
			.Add(search)
		.End()
		.View();

	search->MakeDefault(true);
	page->SetName("Controls");
	return page;
}


static BView*
media_page()
{
	BSlider* position = new BSlider("position", "Position", NULL, 0, 100,
		B_HORIZONTAL, B_TRIANGLE_THUMB);
	position->SetValue(30);
	position->UseFillColor(true, new rgb_color{ 168, 216, 160, 255 });

	BSlider* volume = new BSlider("volume", "Volume", NULL, 0, 100,
		B_HORIZONTAL);
	volume->SetValue(10);
	volume->SetHashMarks(B_HASH_MARKS_BOTTOM);
	volume->SetHashMarkCount(11);

	BSlider* disabled = new BSlider("dis", "Disabled", NULL, 0, 100,
		B_HORIZONTAL);
	disabled->SetValue(60);
	disabled->SetEnabled(false);

	BStatusBar* status = new BStatusBar("status", "Downloading");
	status->SetMaxValue(100);
	status->Update(45);

	BListView* list = new BListView("list");
	const char* names[] = { "BeShare Weekly News (March 29, 2001).txt",
		"BeShare1.90.zip", "pixel32+grabbocam+beshare.jpg",
		"BeShare Member - BGA.txt", "BeShare1.91.zip", "BeShare1.85.zip",
		"BeShare Weekly News (April 5, 2001).txt", "BeShare1.90.zip 3" };
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		list->AddItem(new BStringItem(names[i]));
	list->Select(2);
	BScrollView* scroll = new BScrollView("scroll", list, 0, true, true);

	BView* page = BLayoutBuilder::Group<>(B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(position)
		.Add(volume)
		.Add(disabled)
		.Add(status)
		.Add(scroll)
		.View();
	page->SetName("Media");
	return page;
}


class DemoWindow : public BWindow {
public:
	DemoWindow()
		:
		BWindow(BRect(80, 80, 620, 560), "Gonx controls",
			B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
	{
		BTabView* tabs = new BTabView("tabs");
		tabs->AddTab(controls_page());
		tabs->AddTab(media_page());

		// the concept puts dialog tabs along the bottom
		BTabView* bottom = new BTabView("bottom");
		bottom->SetTabSide(BTabView::kBottomSide);
		BView* mail = BLayoutBuilder::Group<>(B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(new BStringView("hint", "Tabs along the bottom edge"))
			.View();
		mail->SetName("Mail");
		BView* enclosure = new BView("Enclosure", 0);
		bottom->AddTab(mail);
		bottom->AddTab(enclosure);

		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(tabs)
			.Add(bottom)
		.End();
	}
};


int
main()
{
	BApplication app("application/x-vnd.Gonx-ControlsDemo");
	(new DemoWindow())->Show();
	app.Run();
	return 0;
}
