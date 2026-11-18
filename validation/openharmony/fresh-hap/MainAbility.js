import UIAbility from '@ohos:app.ability.UIAbility';

export default class MainAbility extends UIAbility {
    onWindowStageCreate(stage) {
        stage.loadContent('pages/Index');
    }
}
