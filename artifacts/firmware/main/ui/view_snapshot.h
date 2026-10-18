#pragma once
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

// A refresh may free a widget and immediately reuse its address for another
// row. lv_obj_is_valid() is NOT a lifetime check for such an address.
// Delete callbacks clear these references before LVGL releases the allocation.
// Keep references immovable: their addresses are event callback user data.
class SnapshotWidgetRef {
    lv_obj_t *object=nullptr;
    static void deleted(lv_event_t *event) {
        auto *self=(SnapshotWidgetRef*)lv_event_get_user_data(event);
        if(lv_event_get_target(event)==self->object)self->object=nullptr;
    }
public:
    SnapshotWidgetRef()=default;
    SnapshotWidgetRef(const SnapshotWidgetRef&)=delete;
    SnapshotWidgetRef& operator=(const SnapshotWidgetRef&)=delete;
    ~SnapshotWidgetRef(){reset();}
    void reset(lv_obj_t *next=nullptr) {
        // Non-null means DELETE has not run. Called only under the LVGL lock.
        if(object)lv_obj_remove_event_cb_with_user_data(object,deleted,this);
        object=next;
        if(object&&!lv_obj_add_event_cb(object,deleted,LV_EVENT_DELETE,this))
            object=nullptr; // No watcher allocated: never retain an unsafe ref.
    }
    lv_obj_t *get() const {return object;}
};

struct ViewSnapshot {
    struct Scroll {SnapshotWidgetRef object;int32_t x=0,y=0;} scroll[32];
    struct Text {SnapshotWidgetRef object;char value[512]={};} text[8];
    int scroll_count=0,text_count=0;
    SnapshotWidgetRef focused;
};
static void capture_view(lv_obj_t *object,ViewSnapshot &snapshot) {
    if(lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN))return;
    lv_group_t *group=lv_group_get_default();
    // Only capture focus encountered inside this screen, never another root.
    if(group&&lv_group_get_focused(group)==object)snapshot.focused.reset(object);
    if(lv_obj_has_flag(object,LV_OBJ_FLAG_SCROLLABLE)&&snapshot.scroll_count<32) {
        auto &entry=snapshot.scroll[snapshot.scroll_count++];
        entry.object.reset(object);entry.x=lv_obj_get_scroll_x(object);entry.y=lv_obj_get_scroll_y(object);
    }
    if(lv_obj_check_type(object,&lv_textarea_class)&&snapshot.text_count<8) {
        const char *text=lv_textarea_get_text(object);
        // Never overwrite a surviving long draft with a truncated snapshot.
        auto &entry=snapshot.text[snapshot.text_count];
        if(strlen(text)<sizeof(entry.value)){
            snapshot.text_count++;entry.object.reset(object);
            snprintf(entry.value,sizeof(entry.value),"%s",text);
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(object);i++)
        capture_view(lv_obj_get_child(object,i),snapshot);
}
static void restore_view(lv_obj_t *root,const ViewSnapshot &snapshot) {
    for(int i=0;i<snapshot.text_count;i++) {
        const auto &entry=snapshot.text[i];
        // Resolve afresh each iteration: restoring another field can itself
        // invoke a VALUE_CHANGED callback that deletes this field/container.
        lv_obj_t *object=entry.object.get();
        if(object&&strcmp(lv_textarea_get_text(object),entry.value))
            lv_textarea_set_text(object,entry.value);
    }
    lv_obj_update_layout(root);
    for(int i=0;i<snapshot.scroll_count;i++) {
        const auto &entry=snapshot.scroll[i];
        if(lv_obj_t *object=entry.object.get())
            lv_obj_scroll_to(object,entry.x,entry.y,LV_ANIM_OFF);
    }
    if(lv_obj_t *object=snapshot.focused.get())lv_group_focus_obj(object);
}
