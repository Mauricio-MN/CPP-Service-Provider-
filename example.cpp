//ServiceProvider é uma Interface de gerenciamento de dependencias, portanto conhece classe chamada e chamadora (ou suas interfaces)
//Deve ser implementada como <classe Chamada>Service mas referencia somente a Interface, conhecendo as demais apenas a fim de instanciamento
//Classe Chamadora conhece somente a interface da classe chamada e o provedor de dependencia (ServiceProvider)
//Ex: dado a classe chamadora A, interface iB, classe Bnormal e Bdiferente, é criado a classe BService(subClasse de ServiceProvider) e BServiceImpl como singleton,
//a classe BService contem as regras de implementação para a interface iB, que pode ser Bnormal e Bdiferente, é a classe que define qual dessas implementações serve
//a classe chamadora A. Classe A decide se precisa de uma instancia pré pronta do contexto atual, se cria uma nova instancia do contexto atual
//ou se usa uma instancia descartável no contexto atual.
//Classe A precisa necessáriamente receber um shared_pointer de contexto.
//Quando as classes abandonam um contexto ele logo invalida um objeto de ServiceProvider.
//Para obter diferentes comportamentos de uma interface ou mais basta passar um contexto via método, assim a classe que invoca os serviços não fica dependente da implementação prevista a ela, ou use um mapeamento de implementação com classes vazias.
//
// '''Algumas desvantagens: Dependencias fixas de ServiceProvider e Context, diminuição de desempenho, quase toda sobrecarga de dependencia recai em ServiceProvider.
// '''Algumas vantagens: Dependencias apenas de Interfaces (fora ServiceProvider e Context), menos objetos sendo passados como parametros (apenas o contexto basta), chamada de métodos com reflexão (mais de um objeto por contexto).

//iuser.h
class IUser{
    public:
    virtual int getId();
    virtual void setId(int _id);
};

//userstaff.h/.cpp
//#include "iuser.h"
class UserStaff : public IUser{
private: 
    int id;
public:
    UserStaff(int _id){
        id = _id;
    }
    int getId() override{
        return id;
    }
    void setId(int _id) override{
        id = _id;
    }
};

//usernormal.h/.cpp
//#include "iuser.h"
class UserNormal : public IUser{
private: 
    int id;
public:
    UserNormal(int _id){
        id = _id * 2;
    }
    int getId() override{
        return id;
    }
    void setId(int _id) override{
        id = _id * 2;
    }
};

//teste.h/.cpp
//#include "userservice.h" //<- "isuer.h" ja incluído
class Teste_Alt{
};

class Teste{
    private:
    CONTEXT context;
    public:
    Teste(CONTEXT _context, int user_Id){
        context = g_UserService.addInstance(context, *this);
        //Outra classe instanciada para funcionar com Teste_Alt.
        g_UserService.addInstance<Teste_Alt>(context);

        //2 calls -> UserStaff.setId(user_Id) e UserNormal.setId(user_Id)
        g_UserService.callMethod(context, &IUser::setId, user_Id);

        std::vector<int> value = g_UserService.callMethodGet(context, &IUser::getId);
        auto valueT = g_UserService.callMethodGet(context, &IUser::setId, 2);
    }
};

////userservice.h/.cpp | quase toda sobrecarga de dependencia recai em ServiceProvider.
//#include "teste.h"
//#include "iuser.h"
//#include "usernormal.h"
//#include "userstaff.h"
//#include "service.h"
class UserService : public service::ServiceProvider<IUser>
{
    IUser getNewInstance(std::type_info& type) override{
        if(type == typeid(Teste)){
            return UserStaff(0);
        } else if(type == typeid(Teste_Alt)){
            return UserNormal(0);
        } else {
            return UserNormal(0);
        }
    }
};

//opicional
static UserService g_UserService;
