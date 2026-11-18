class FreshInstallPage extends ViewPU {
    constructor() {
        super(undefined, undefined, -1);
        this.status = new ObservedPropertySimplePU('Olympium fresh HAP', this, 'status');
        this.finalizeConstruction();
    }
    initialRender() {
        this.observeComponentCreation2(() => {
            Column.create();
            Column.width('100%');
            Column.height('100%');
            Column.justifyContent(FlexAlign.Center);
        }, Column);
        this.observeComponentCreation2(() => {
            Text.create(this.status.get());
            Text.fontSize(24);
        }, Text);
        Text.pop();
        this.observeComponentCreation2(() => {
            Button.createWithLabel('Check input');
            Button.id('verify-fresh-hap');
            Button.onClick(() => {
                this.status.set('Fresh HAP input received');
            });
        }, Button);
        Button.pop();
        Column.pop();
    }
    rerender() {
        this.updateDirtyElements();
    }
    purgeVariableDependenciesOnElmtId(id) {
        this.status.purgeDependencyOnElmtId(id);
    }
    aboutToBeDeleted() {
        this.status.aboutToBeDeleted();
        SubscriberManager.Get().delete(this.id__());
        this.aboutToBeDeletedInternal();
    }
}
ViewStackProcessor.StartGetAccessRecordingFor(ViewStackProcessor.AllocateNewElmetIdForNextComponent());
loadDocument(new FreshInstallPage());
ViewStackProcessor.StopGetAccessRecording();
