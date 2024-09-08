// https://github.com/grrowl/homebridge-plugin-automation
// this script was written for homebridge-plugin-automation@1.0.2

const INITIATOR_SERVICE_NAME = '';
const RECEIVER_SERVICE_NAME = '';

automation.listen(function (event) {
  const initiatorOnStatus = event.serviceCharacteristics.find((c) => c.type === "On");
  const returnValue = {
    serviceName: event.serviceName,
    status: !!initiatorOnStatus
      ? (initiatorOnStatus.value ? 'turned on' : 'turned off')
      : null,
    actionTaken: false,
    message: ''
    // uncomment for more involved debugging
    // event: JSON.stringify(event),
  };

  if (event.serviceName !== INITIATOR_SERVICE_NAME) {
    returnValue.message = `event not for ${INITIATOR_SERVICE_NAME}`;
    return returnValue;
  }

  if (initiatorOnStatus) {
    const receiverService = automation.services.find(
      (s) => s.serviceName === RECEIVER_SERVICE_NAME,
    );

    if (!receiverService) {
      returnValue.message = `could not find ${RECEIVER_SERVICE_NAME}, returned: ${JSON.stringify(receiverService)}`;
      return returnValue;
    }

    const receiverOnStatus = receiverService.serviceCharacteristics.find((s) => s.type === "On");
    if (!receiverOnStatus) {
      returnValue.message = `could not find receiver onStatus, returned: ${JSON.stringify(receiverOnStatus)}`;
      return returnValue;
    }

    automation.set(receiverService.uniqueId, receiverOnStatus.iid, initiatorOnStatus.value);

    returnValue.actionTaken = true;
    returnValue.message = 'success';
    return returnValue;
  }

  return null;
});